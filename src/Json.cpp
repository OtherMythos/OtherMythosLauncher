#include "Json.h"

#include <cJSON.h>

namespace OML{
    JsonDocument::JsonDocument(const std::string& text)
        : mRoot(cJSON_ParseWithLength(text.data(), text.size())) {
    }

    JsonDocument::~JsonDocument(){
        cJSON_Delete(mRoot);
    }

    bool JsonDocument::isObject() const{
        return mRoot && cJSON_IsObject(mRoot);
    }

    const cJSON* jsonChild(const cJSON* object, const char* key){
        if(!object || !cJSON_IsObject(object)) return nullptr;
        return cJSON_GetObjectItemCaseSensitive(object, key);
    }

    std::string jsonString(const cJSON* object, const char* key, const std::string& fallback){
        const cJSON* item = jsonChild(object, key);
        if(!cJSON_IsString(item) || !item->valuestring) return fallback;
        return item->valuestring;
    }

    double jsonNumber(const cJSON* object, const char* key, double fallback){
        const cJSON* item = jsonChild(object, key);
        if(!cJSON_IsNumber(item)) return fallback;
        return item->valuedouble;
    }

    bool jsonBool(const cJSON* object, const char* key, bool fallback){
        const cJSON* item = jsonChild(object, key);
        if(!cJSON_IsBool(item)) return fallback;
        return cJSON_IsTrue(item);
    }

    std::string jsonPrintAndDelete(cJSON* object){
        char* printed = cJSON_Print(object);
        std::string out = printed ? printed : "";
        cJSON_free(printed);
        cJSON_Delete(object);
        return out + "\n";
    }
}
