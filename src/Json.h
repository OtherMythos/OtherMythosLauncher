#pragma once

#include <string>

struct cJSON;

namespace OML{
    //Owns a parsed cJSON tree for the lifetime of the object.
    class JsonDocument{
    public:
        explicit JsonDocument(const std::string& text);
        ~JsonDocument();
        JsonDocument(const JsonDocument&) = delete;
        JsonDocument& operator=(const JsonDocument&) = delete;

        const cJSON* root() const { return mRoot; }
        bool isObject() const;

    private:
        cJSON* mRoot;
    };

    const cJSON* jsonChild(const cJSON* object, const char* key);
    std::string jsonString(const cJSON* object, const char* key, const std::string& fallback = "");
    //cJSON's valueint saturates at INT_MAX, so sizes are read through the double.
    double jsonNumber(const cJSON* object, const char* key, double fallback = 0.0);
    bool jsonBool(const cJSON* object, const char* key, bool fallback = false);

    //Prints and frees the tree.
    std::string jsonPrintAndDelete(cJSON* object);
}
