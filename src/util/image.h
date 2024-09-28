
#pragma once

#include <rpp/base.h>
#include <rpp/files.h>
#include <stb/stb_image.h>

using namespace rpp;

template<Allocator A = Mdefault>
struct Image {

    explicit Image() = default;
    ~Image() = default;

    Image(const Image& src) = delete;
    Image& operator=(const Image& src) = delete;

    Image(Image&& src) = default;
    Image& operator=(Image&& src) = default;

    Image(u64 w, u64 h, Vec<u8, A>&& data) : _w(w), _h(h), _data(move(data)) {
    }

    u64 w() const {
        return _w;
    }
    u64 h() const {
        return _h;
    }
    u64 bytes() const {
        return _w * _h * 4;
    }
    Pair<u64, u64> dim() const {
        return {_w, _h};
    }

    Slice<const u8> data() const {
        return _data;
    }

    bool reload(String_View path) {

        auto file_data = Files::read(path);
        if(!file_data.ok()) return false;

        int x, y;
        unsigned char* pixels = stbi_load_from_memory(file_data->data(), file_data->length(), &x,
                                                      &y, null, STBI_rgb_alpha);
        if(!pixels) return false;

        _data.clear();
        _data.extend(x * y * 4);
        _w = x;
        _h = y;

        std::memcpy(_data.data(), pixels, _data.length());
        stbi_image_free(pixels);

        return true;
    }

    static Opt<Image> load(String_View path) {
        Image ret;
        if(ret.reload(path)) {
            return ret;
        }
        return {};
    }

private:
    Vec<u8, A> _data;
    u64 _w = 0, _h = 0;
};
