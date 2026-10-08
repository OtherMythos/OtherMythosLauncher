#include "Config.h"

#include "BuildIndex.h"
#include "Json.h"
#include "Paths.h"

#include <algorithm>
#include <cJSON.h>

namespace OML{
    static const char* const kPublicIndex = "https://builds.othermythos.com/index.json";

    std::string sanitizeName(const std::string& name){
        std::string out;
        for(char c : name){
            bool ok = (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-' || c == '_';
            out.push_back(ok ? c : '_');
        }
        return out.empty() ? "source" : out;
    }

    static void makeSourceNamesUnique(Config& config){
        std::vector<std::string> seen;
        for(SourceConfig& s : config.sources){
            std::string base = sanitizeName(s.name.empty() ? urlHost(s.url) : s.name);
            std::string name = base;
            for(int i = 2; std::find(seen.begin(), seen.end(), name) != seen.end(); i++){
                name = base + "-" + std::to_string(i);
            }
            s.name = name;
            seen.push_back(name);
        }
    }

    Config defaultConfig(){
        Config config;
        config.sources.push_back({"public", kPublicIndex});
        return config;
    }

    std::string configToJson(const Config& config){
        cJSON* root = cJSON_CreateObject();
        cJSON* sources = cJSON_AddArrayToObject(root, "sources");
        for(const SourceConfig& s : config.sources){
            cJSON* source = cJSON_CreateObject();
            cJSON_AddStringToObject(source, "name", s.name.c_str());
            cJSON_AddStringToObject(source, "url", s.url.c_str());
            cJSON_AddItemToArray(sources, source);
        }
        cJSON* hidden = cJSON_AddArrayToObject(root, "hiddenProjects");
        for(const std::string& p : config.hiddenProjects){
            cJSON_AddItemToArray(hidden, cJSON_CreateString(p.c_str()));
        }
        cJSON* titles = cJSON_AddObjectToObject(root, "titles");
        for(const auto& t : config.titles){
            cJSON_AddStringToObject(titles, t.first.c_str(), t.second.c_str());
        }
        const char* fullscreen = config.fullscreen == FullscreenMode::ON ? "on" : config.fullscreen == FullscreenMode::OFF ? "off" : "auto";
        cJSON_AddStringToObject(root, "fullscreen", fullscreen);
        return jsonPrintAndDelete(root);
    }

    bool configFromJson(const std::string& json, Config& config, std::string& error){
        JsonDocument doc(json);
        if(!doc.isObject()){
            error = "config.json isn't a JSON object";
            return false;
        }
        Config parsed;
        const cJSON* source = nullptr;
        cJSON_ArrayForEach(source, jsonChild(doc.root(), "sources")){
            std::string url = jsonString(source, "url");
            if(url.empty()) continue;
            parsed.sources.push_back({jsonString(source, "name"), url});
        }
        const cJSON* hidden = nullptr;
        cJSON_ArrayForEach(hidden, jsonChild(doc.root(), "hiddenProjects")){
            if(cJSON_IsString(hidden)) parsed.hiddenProjects.push_back(hidden->valuestring);
        }
        const cJSON* title = nullptr;
        cJSON_ArrayForEach(title, jsonChild(doc.root(), "titles")){
            if(cJSON_IsString(title) && title->string) parsed.titles[title->string] = title->valuestring;
        }
        std::string fullscreen = jsonString(doc.root(), "fullscreen", "auto");
        parsed.fullscreen = fullscreen == "on" ? FullscreenMode::ON : fullscreen == "off" ? FullscreenMode::OFF : FullscreenMode::AUTO;
        makeSourceNamesUnique(parsed);
        config = parsed;
        return true;
    }

    Config loadOrCreateConfig(const std::filesystem::path& path, std::string& error){
        std::string text;
        if(!readFile(path, text)){
            Config config = defaultConfig();
            writeFileAtomically(path, configToJson(config));
            return config;
        }
        Config config;
        if(!configFromJson(text, config, error)){
            error = pathToUtf8(path) + ": " + error + ". Using the default sources until it's fixed.";
            return defaultConfig();
        }
        return config;
    }

    void addSourceFirst(Config& config, const std::string& url){
        config.sources.erase(std::remove_if(config.sources.begin(), config.sources.end(),
            [&](const SourceConfig& s){ return s.url == url; }), config.sources.end());
        config.sources.insert(config.sources.begin(), {"", url});
        makeSourceNamesUnique(config);
    }
}
