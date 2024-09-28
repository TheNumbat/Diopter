
#pragma once

#include <imgui/imgui.h>
#include <rpp/base.h>

using namespace rpp;

namespace ImGui {

using namespace Reflect;

const ImGuiWindowFlags DebugWin =
    ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoFocusOnAppearing;

template<Allocator A>
bool InputText(String_View label, String<A>& buf, ImGuiInputTextFlags flags = 0,
               ImGuiInputTextCallback callback = null, void* user_data = null) {

    return InputText(label.data(), buf.data(), buf.capacity(), flags, callback, user_data);
}

inline void Text(String_View text) {
    TextUnformatted(reinterpret_cast<const char*>(text.data()),
                    reinterpret_cast<const char*>(text.data() + text.length() - 1));
}

inline bool DragU64(const char* label, u64* v, f32 v_speed = 1.0f, u64 v_min = 0, u64 v_max = 0,
                    const char* format = "%lu", ImGuiSliderFlags flags = 0) {
    return DragScalar(label, ImGuiDataType_U64, v, v_speed, &v_min, &v_max, format, flags);
}

inline bool DragI64(const char* label, i64* v, f32 v_speed = 1.0f, i64 v_min = 0, i64 v_max = 0,
                    const char* format = "%ld", ImGuiSliderFlags flags = 0) {
    return DragScalar(label, ImGuiDataType_S64, v, v_speed, &v_min, &v_max, format, flags);
}

inline bool SliderU32(const char* label, u32* v, u32 v_min, u32 v_max, const char* format = "%lu",
                      ImGuiSliderFlags flags = 0) {
    return SliderScalar(label, ImGuiDataType_U32, v, &v_min, &v_max, format, flags);
}

inline bool SliderI32(const char* label, i32* v, i32 v_min, i32 v_max, const char* format = "%ld",
                      ImGuiSliderFlags flags = 0) {
    return SliderScalar(label, ImGuiDataType_S32, v, &v_min, &v_max, format, flags);
}

inline bool SliderU64(const char* label, u64* v, u64 v_min, u64 v_max, const char* format = "%lu",
                      ImGuiSliderFlags flags = 0) {
    return SliderScalar(label, ImGuiDataType_U64, v, &v_min, &v_max, format, flags);
}

inline bool SliderI64(const char* label, i64* v, i64 v_min, i64 v_max, const char* format = "%ld",
                      ImGuiSliderFlags flags = 0) {
    return SliderScalar(label, ImGuiDataType_S64, v, &v_min, &v_max, format, flags);
}

template<Enum E>
bool Combo(const char* label, E& val, ImGuiComboFlags flags = 0) {

    bool ret = false;

    if(BeginCombo(label, Format::enum_name(val), flags)) {

        iterate_enum<E>([&](const Literal& name, const E& value) {
            bool selected = val == value;
            if(Selectable(name, selected)) {
                val = value;
                ret = true;
            }
            if(selected) SetItemDefaultFocus();
        });

        EndCombo();
    }

    return ret;
}

template<typename V, Allocator A>
bool Combo(const char* label, Map<String_View, V, A> options, V& val, ImGuiComboFlags flags = 0) {

    String_View preview;
    for(auto& opt : options) {
        if(opt.second == val) preview = opt.first;
    }

    bool ret = false;

    Region(R) {
        if(BeginCombo(
               label,
               reinterpret_cast<const char*>(preview.template terminate<Mregion<R>>().data()),
               flags)) {
            for(auto& opt : options) {
                bool selected = val == opt.second;
                if(Selectable(reinterpret_cast<const char*>(
                                  opt.first.template terminate<Mregion<R>>().data()),
                              selected)) {
                    val = opt.second;
                    ret = true;
                }
                if(selected) SetItemDefaultFocus();
            }

            EndCombo();
        }
    }
    return ret;
}

inline void DecorateIndex(u64 i) {
    static constexpr ImVec4 idx_col{0.8f, 0.8f, 0.8f, 0.8f};

    TextColored(idx_col, "[%d]", i);
    SameLine();
    SetCursorPosX(GetCursorPosX() - 3.0f);
}

template<Reflectable T>
void DecorateName(const char* label) {
    static constexpr ImVec4 type_col{0.8f, 0.8f, 0.8f, 0.8f};

    Region(R) {
        auto tname = format_typename<T, Mregion<R>>();
        TextColored(type_col, "%.*s", tname.length(), reinterpret_cast<const char*>(tname.data()));
    }

    SameLine();
    SetCursorPosX(GetCursorPosX() - 3.0f);
    Text("%s =", label);
    SameLine();
    SetCursorPosX(GetCursorPosX() - 3.0f);
}

template<Reflectable T>
struct View;

struct Record_View {
    template<Reflectable T>
    void apply(const Literal& name, const T& value) {
        PushID(idx++);
        View<Decay<T>>::decorate(name, value, open);
        PopID();
    }
    u64 idx = 0;
    bool open = false;
};

struct Record_Edit {
    template<Reflectable T>
    void apply(const Literal& name, T& value) {
        PushID(idx++);
        View<Decay<T>>::edit(name, value, open);
        PopID();
    }
    u64 idx = 0;
    bool open = false;
};

template<Reflectable T>
struct View {

    static void decorate(const char* label, const T& value, bool open) {
        DecorateName<T>(label);
        plain(value, open);
    }

    static void plain(const T& value, bool open) {
        using R = Refl<T>;

        if constexpr(R::kind == Kind::char_) {
            Text("%c", value);
        } else if constexpr(R::kind == Kind::i8_) {
            Text("%hhd", value);
        } else if constexpr(R::kind == Kind::i16_) {
            Text("%hd", value);
        } else if constexpr(R::kind == Kind::i32_) {
            Text("%d", value);
        } else if constexpr(R::kind == Kind::i64_) {
#ifdef RPP_COMPILER_MSVC
            Text("%lld", value);
#else
            Text("%ld", value);
#endif
        } else if constexpr(R::kind == Kind::u8_) {
            Text("%hhu", value);
        } else if constexpr(R::kind == Kind::u16_) {
            Text("%hu", value);
        } else if constexpr(R::kind == Kind::u32_) {
            Text("%u", value);
        } else if constexpr(R::kind == Kind::u64_) {
#ifdef RPP_COMPILER_MSVC
            Text("%llu", value);
#else
            Text("%lu", value);
#endif
        } else if constexpr(R::kind == Kind::f32_) {
            Text("%f", value);
        } else if constexpr(R::kind == Kind::f64_) {
            Text("%f", value);
        } else if constexpr(R::kind == Kind::bool_) {
            Text("%s", value ? "true" : "false");
        } else if constexpr(R::kind == Kind::array_) {
            if(TreeNodeEx("", open ? ImGuiTreeNodeFlags_DefaultOpen : 0)) {
                for(u64 i = 0; i < R::length; i++) {
                    PushID(i);
                    DecorateIndex(i);
                    View<typename R::underlying>::plain(value[i], open);
                    PopID();
                }
                TreePop();
            }
        } else if constexpr(R::kind == Kind::pointer_) {
            Text("%p", value);
        } else if constexpr(R::kind == Kind::record_) {
            if(TreeNodeEx("", open ? ImGuiTreeNodeFlags_DefaultOpen : 0)) {
                iterate_record(Record_View{0, open}, value);
                TreePop();
            }
        } else if constexpr(R::kind == Kind::enum_) {
            Text("%s::%s", R::name, enum_name(value));
        }
    }

    static void edit(const char* label, T& value, bool open) {
        using R = Refl<T>;

        if constexpr(R::kind == Kind::char_) {
            DragScalar(label, ImGuiDataType_S8, &value, 1.0f, null, null, "%c");
        } else if constexpr(R::kind == Kind::i8_) {
            DragScalar(label, ImGuiDataType_S8, &value);
        } else if constexpr(R::kind == Kind::i16_) {
            DragScalar(label, ImGuiDataType_S16, &value);
        } else if constexpr(R::kind == Kind::i32_) {
            DragScalar(label, ImGuiDataType_S32, &value);
        } else if constexpr(R::kind == Kind::i64_) {
            DragScalar(label, ImGuiDataType_S64, &value);
        } else if constexpr(R::kind == Kind::u8_) {
            DragScalar(label, ImGuiDataType_U8, &value);
        } else if constexpr(R::kind == Kind::u16_) {
            DragScalar(label, ImGuiDataType_U16, &value);
        } else if constexpr(R::kind == Kind::u32_) {
            DragScalar(label, ImGuiDataType_U32, &value);
        } else if constexpr(R::kind == Kind::u64_) {
            DragScalar(label, ImGuiDataType_U64, &value);
        } else if constexpr(R::kind == Kind::f32_) {
            DragScalar(label, ImGuiDataType_Float, &value);
        } else if constexpr(R::kind == Kind::f64_) {
            DragScalar(label, ImGuiDataType_Double, &value);
        } else if constexpr(R::kind == Kind::bool_) {
            Checkbox(label, &value);
        } else if constexpr(R::kind == Kind::array_) {
            Region(R0) {
                auto tname = format_typename<T, Mregion<R0>>();
                if(TreeNodeEx(label, open ? ImGuiTreeNodeFlags_DefaultOpen : 0, "%.*s %s",
                              tname.length(), reinterpret_cast<const char*>(tname.data()), label)) {
                    for(u64 i = 0; i < R::length; i++) {
                        PushID(i);
                        Region(R1) {
                            const char* idx = reinterpret_cast<const char*>(
                                format<Mregion<R1>>("[%]\x00"_v, i).data());
                            View<typename R::underlying>::edit(idx, value[i], open);
                        }
                        PopID();
                    }
                    TreePop();
                }
            }
        } else if constexpr(R::kind == Kind::pointer_) {
            Region(R) {
                auto tname = format_typename<T, Mregion<R>>();
                Text("%.*s %s = %p", tname.length(), reinterpret_cast<const char*>(tname.data()),
                     label, value);
            }
        } else if constexpr(R::kind == Kind::record_) {
            Region(R) {
                auto tname = format_typename<T, Mregion<R>>();
                if(TreeNodeEx(label, open ? ImGuiTreeNodeFlags_DefaultOpen : 0, "%.*s %s",
                              tname.length(), reinterpret_cast<const char*>(tname.data()), label)) {
                    iterate_record(Record_Edit{0, open}, value);
                    TreePop();
                }
            }
        } else if constexpr(R::kind == Kind::enum_) {
            Combo(label, value);
        }
    }
};

template<Reflectable T>
struct View<Vec<T>> {

    static void decorate(const char* label, const Vec<T>& value, bool open) {
        DecorateName<Vec<T>>(label);
        plain(value, open);
    }

    static void plain(const Vec<T>& value, bool open) {
        if(TreeNodeEx("", open ? ImGuiTreeNodeFlags_DefaultOpen : 0)) {
            for(u64 i = 0; i < value.length(); i++) {
                PushID(i);
                DecorateIndex(i);
                View<Decay<T>>::plain(value[i], open);
                PopID();
            }
            TreePop();
        }
    }

    static void edit(const char* label, Vec<T>& value, bool open) {
        Region(R0) {
            auto tname = format_typename<Vec<T>, Mregion<R0>>();
            if(TreeNodeEx(label, open ? ImGuiTreeNodeFlags_DefaultOpen : 0, "%.*s %s",
                          tname.length(), reinterpret_cast<const char*>(tname.data()), label)) {
                for(u64 i = 0; i < value.length(); i++) {
                    PushID(i);
                    Region(R1) {
                        const char* idx = reinterpret_cast<const char*>(
                            format<Mregion<R1>>("[%]\x00"_v, i).data());
                        View<Decay<T>>::edit(idx, value[i], open);
                    }
                    PopID();
                }
                TreePop();
            }
        }
    }
};

template<Reflectable T>
void Render(const char* label, const T& val, bool open = false) {
    PushID(&val);
    View<T>::decorate(label, val, open);
    PopID();
}

template<Reflectable T>
void Edit(const char* label, T& val, bool open = false) {
    PushID(&val);
    View<T>::edit(label, val, open);
    PopID();
}

} // namespace ImGui
