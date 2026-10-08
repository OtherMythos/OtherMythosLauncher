#include "SafePath.h"

namespace OML{
    bool isSafeName(const std::string& name){
        if(name.empty() || name.size() > 255) return false;
        if(name == "." || name == "..") return false;
        //Windows silently strips trailing dots and spaces, so 'a.' and 'a' would collide.
        if(name.back() == '.' || name.back() == ' ') return false;
        for(unsigned char c : name){
            if(c < 0x20 || c == 0x7f) return false;
            switch(c){
                case '/': case '\\': case ':': case '*': case '?': case '"': case '<': case '>': case '|':
                    return false;
                default:
                    break;
            }
        }
        return true;
    }

    bool isSafeRelativePath(const std::string& path){
        if(path.empty() || path.size() > 1024) return false;
        size_t start = 0;
        while(true){
            size_t end = path.find('/', start);
            std::string part = path.substr(start, end == std::string::npos ? std::string::npos : end - start);
            if(!isSafeName(part)) return false;
            if(end == std::string::npos) return true;
            start = end + 1;
        }
    }
}
