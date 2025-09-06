//=============================================================================
// src/backend_trtllm.cpp
// Minimal TensorRT-LLM backend wrapper (skeleton).
// Build only when HAVE_TRTLLM=1 with proper includes/libs.
// License: BSD3
//=============================================================================
#include "../include/llm_backend.h"
#include <string.h>
#include <stdlib.h>

static char *dupstr(const char *s){ if(!s) return NULL; size_t n=strlen(s)+1; char *p=(char*)malloc(n); if(p) memcpy(p,s,n); return p; }

#ifdef HAVE_TRTLLM
#include <tensorrt_llm/executor/executor.h>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
// Include SentencePiece for tokenization (if using SentencePiece-based tokenizer):
#include <sentencepiece_processor.h>

static tensorrt_llm::executor::Executor *g_executor = nullptr;
static sentencepiece::SentencePieceProcessor g_sp;  // tokenizer

static int ensure_engine(const char *path) {
    if (!path) return -1;
    std::filesystem::path modelDir(path);
    if (!std::filesystem::exists(modelDir)) {
        fprintf(stderr, "Engine path not found: %s\n", path);
        return -1;
    }
    // If a "1" subdirectory exists (Triton format), use that as modelDir
    std::filesystem::path subDir = modelDir / "1";
    if (std::filesystem::exists(subDir) && std::filesystem::is_directory(subDir)) {
        modelDir = subDir;
    }

    // Load model config and tokenizer
    std::filesystem::path configPath = modelDir / "config.json";
    std::filesystem::path genConfigPath = modelDir / "generation_config.json";
    std::filesystem::path spModelPath = modelDir.parent_path() / "tokenizer.model"; 
    // (tokenizer.model is usually in the parent directory as per TRT-LLM structure:contentReference[oaicite:3]{index=3})

    // (Optional) Read config.json for model parameters (e.g., vocab size or EOS id)
    int eos_token_id = -1, pad_token_id = -1;
    if (std::filesystem::exists(genConfigPath)) {
        std::ifstream genCfgFile(genConfigPath);
        nlohmann::json genCfg; 
        genCfgFile >> genCfg;
        // generation_config.json may contain eos_token_id and pad_token_id
        if (genCfg.contains("eos_token_id")) {
            // Handle if eos_token_id is an array or single int
            if (genCfg["eos_token_id"].is_number_integer()) {
                eos_token_id = genCfg["eos_token_id"].get<int>();
            } else if (genCfg["eos_token_id"].is_array() && !genCfg["eos_token_id"].empty()) {
                eos_token_id = genCfg["eos_token_id"][0].get<int>();
            }
        }
        if (genCfg.contains("pad_token_id") && genCfg["pad_token_id"].is_number_integer()) {
            pad_token_id = genCfg["pad_token_id"].get<int>();
        }
    } else if (std::filesystem::exists(configPath)) {
        // Fallback: check config.json for special tokens
        std::ifstream cfgFile(configPath);
        nlohmann::json cfg; 
        cfgFile >> cfg;
        if (cfg.contains("eos_token_id")) {
            eos_token_id = cfg["eos_token_id"].get<int>();
        }
        if (cfg.contains("pad_token_id")) {
            pad_token_id = cfg["pad_token_id"].get<int>();
        }
    }

    // Initialize the SentencePiece tokenizer if available
    if (std::filesystem::exists(spModelPath)) {
        if (!g_sp.Load(spModelPath.string()).ok()) {
            fprintf(stderr, "Failed to load tokenizer model: %s\n", spModelPath.c_str());
            // Not fatal; we can still generate token IDs if tokens are provided
        }
    }

    // Create Executor (engine) if not already created
    if (!g_executor) {
        try {
            // Configure Executor. ModelType::kDECODER_ONLY for GPT-like models.
            using namespace tensorrt_llm::executor;
            ExecutorConfig exeConfig;  // use default config (batchingType = kINFLIGHT, etc.)
            // If multiple rank engines are present, set up parallel config for multi-GPU
            std::vector<std::filesystem::path> engineFiles;
            for (auto &file : std::filesystem::directory_iterator(modelDir)) {
                if (file.path().extension() == ".engine") {
                    engineFiles.push_back(file.path());
                }
            }
            size_t world_size = engineFiles.size();
            if (world_size > 1) {
                // Multi-GPU engine: use MPI for communication
                ParallelConfig pc;
                std::vector<SizeType32> deviceIds, ranks;
                for (SizeType32 i = 0; i < world_size; ++i) {
                    deviceIds.push_back(i);
                    ranks.push_back(i);
                }
                pc.setDeviceIds(deviceIds);
                pc.setParticipantIds(ranks);
                // (By default, commType = kMPI, commMode = kLEADER for rank0)
                exeConfig.setParallelConfig(pc);
                // Note: The program should be run with `mpirun -n <world_size>` so each rank loads its shard.
            }
            // Initialize the Executor with the model directory
            g_executor = new Executor(modelDir, ModelType::kDECODER_ONLY, exeConfig);
        } catch (const std::exception &e) {
            fprintf(stderr, "TensorRT-LLM engine initialization failed: %s\n", e.what());
            return -1;
        }
    }
    return 0;
}

static char* trt_generate(const struct llm_req *r) {
    if (!r) return dupstr("");  // or return nullptr on error
    if (!g_executor) {
        // Engine not initialized; try to initialize with default path (if known)
        // For safety, you might call ensure_engine with a preset model path or return an error.
        fprintf(stderr, "TRT-LLM engine not initialized\n");
        return dupstr("");
    }

    std::string prompt = r->prompt;   // assuming llm_req has a C-string prompt
    uint32_t max_new_tokens = r->max_tokens;  // maximum tokens to generate
    if (max_new_tokens == 0) {
        max_new_tokens = 100; // default if not specified
    }

    // 1. Tokenize the prompt text to input token IDs
    std::vector<int> input_ids;
    if (prompt.size() > 0) {
        if (g_sp.IsLoaded()) {
            // Use SentencePiece to encode text to token IDs
            g_sp.Encode(prompt, &input_ids);
        } else {
            fprintf(stderr, "No tokenizer loaded, cannot encode prompt.\n");
            return dupstr("");
        }
    }
    if (input_ids.empty()) {
        // If empty prompt, you may want to handle differently (e.g., BOS token)
        // For simplicity, assume prompt is not empty.
    }

    // 2. Set up sampling parameters
    using namespace tensorrt_llm::executor;
    SamplingConfig samplingCfg;  // default: beamWidth=1, no sampling constraints
    // Apply user-specified decoding params (if provided in llm_req)
    if (r->top_k >= 0) {
        // Note: top_k == 0 means greedy (no top-K filtering)
        samplingCfg.setTopK((r->top_k > 0) ? std::optional<SizeType32>(r->top_k)
                                          : std::optional<SizeType32>(0));
    }
    if (r->top_p >= 0.0f) {
        // top_p == 0.0 indicates greedy (no top-P filtering)
        samplingCfg.setTopP((r->top_p > 0.0f) ? std::optional<float>(r->top_p)
                                             : std::optional<float>(0.0f));
    }
    if (r->temperature > 0.0f) {
        // temperature = 0 can be treated as deterministic (extreme case)
        float temp = r->temperature;
        if (temp <= 0.0f) temp = 0.0f;
        samplingCfg.setTemperature(temp);
    }
    if (r->repetition_penalty != 0.0f) {
        // (If 1.0 means no penalty, >1 discourages repeats)
        samplingCfg.setRepetitionPenalty(std::optional<float>(r->repetition_penalty));
    }
    if (r->presence_penalty != 0.0f) {
        samplingCfg.setPresencePenalty(std::optional<float>(r->presence_penalty));
    }
    if (r->frequency_penalty != 0.0f) {
        samplingCfg.setFrequencyPenalty(std::optional<float>(r->frequency_penalty));
    }
    if (r->seed != 0) {  // if seed provided (non-zero)
        samplingCfg.setSeed(std::optional<RandomSeedType>(r->seed));
    }

    // 3. Create a generation request for the Executor
    std::optional<TokenIdType> optEndId = std::nullopt;
    std::optional<TokenIdType> optPadId = std::nullopt;
    // Use EOS/PAD IDs obtained in ensure_engine (if any were set there)
    // Suppose we stored eos_token_id and pad_token_id in static vars or accessible config.
    if (eos_token_id >= 0) optEndId = eos_token_id;
    if (pad_token_id >= 0) optPadId = pad_token_id;
    Request req(
        input_ids, 
        max_new_tokens, 
        false,                 // no streaming, gather full output 
        samplingCfg, 
        OutputConfig(),        // default output config (no logits returned, etc.) 
        optEndId, 
        optPadId
        // (stopWords, etc., could be added here if needed)
    );

    // 4. Enqueue the request and wait for the result
    IdType reqId = g_executor->enqueueRequest(req);
    // Wait for the final response (non-streaming mode returns one final result)
    std::vector<Response> responses = g_executor->awaitResponses(reqId);
    if (responses.empty()) {
        fprintf(stderr, "No response from TRT-LLM executor\n");
        return dupstr("");
    }
    Response resp = responses.front();
    if (resp.hasError()) {
        // If the generation failed, log error
        std::string err = resp.getErrorMsg();
        fprintf(stderr, "Generation error: %s\n", err.c_str());
        return dupstr("");
    }
    // 5. Extract generated tokens from the response
    const auto &result = resp.getResult();
    // result.outputTokenIds is a vector of token sequences (one per beam)
    std::vector<TokenIdType> output_ids;
    if (!result.outputTokenIds.empty()) {
        // We had beamWidth=1, so take the first (and only) beam's tokens
        output_ids = result.outputTokenIds[0];
    }
    // If an EOS token was generated at end, remove it from output_ids
    if (!output_ids.empty() && eos_token_id >= 0 && output_ids.back() == eos_token_id) {
        output_ids.pop_back();
    }

    // 6. Decode output tokens back to text
    std::string generated_text;
    if (g_sp.IsLoaded()) {
        // Use SentencePiece to decode token IDs to text
        g_sp.Decode(output_ids, &generated_text);
    } else {
        // If no tokenizer loaded, return tokens as a space-separated string as fallback
        for (size_t i = 0; i < output_ids.size(); ++i) {
            generated_text += std::to_string(output_ids[i]);
            if (i + 1 < output_ids.size()) generated_text += " ";
        }
    }

    // Return the generated text (allocated on heap, caller will free)
    return dupstr(generated_text.c_str());
}

extern "C" int llm_trtllm_complete(const struct llm_req *r, struct llm_resp *out){
	memset(out,0,sizeof *out);
	if(ensure_engine(r->trt_engine_path? r->trt_engine_path : "engine.plan")<0){
		out->status=2; out->err=dupstr("failed to init TRT-LLM engine"); return -1;
	}
	char *txt = trt_generate(r);
	if(!txt){ out->status=3; out->err=dupstr("TRT-LLM generation failed"); return -1; }
	out->content = txt; out->status=0; out->http_status=0;
	return 0;
}
#else
extern "C" int llm_trtllm_complete(const struct llm_req *r, struct llm_resp *out){
	(void)r; (void)out;
	/* If this file is compiled without HAVE_TRTLLM=1, it's a build config error. */
	return -1;
}
#endif
