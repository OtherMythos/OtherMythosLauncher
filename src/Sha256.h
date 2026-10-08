#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

namespace OML{
    class Sha256{
    public:
        Sha256();

        void update(const void* data, size_t size);
        //Lowercase hex digest. The object can't be updated afterwards.
        std::string finish();

        static std::string hashString(const std::string& data);

    private:
        void processBlock(const uint8_t* block);

        uint32_t mState[8];
        uint8_t mBuffer[64];
        size_t mBufferSize;
        uint64_t mTotalSize;
    };
}
