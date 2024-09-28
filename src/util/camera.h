
#pragma once

#include <rpp/base.h>
#include <rpp/tuple.h>

#include "../platform/window.h"

using namespace rpp;

struct Camera {

    enum class Mode : u8 { orbit, first };
    struct Orbit {
        enum class Control : u8 {
            none,
            orbit,
            move,
        };
        Control control = Control::none;
        Vec3 at;
        Quat rot;
        f32 radius = 1.0f;
        f32 ob_sens = 0.2f;
        f32 mv_sens = 0.015f;
        f32 rd_sens = 0.25f;
    };
    struct First {
        Vec3 pos;
        Quat rot;
        f32 v_sens = 0.05f;
        f32 speed = 2.5f;
    };

    explicit Camera(Window& window);

    Mat4 view() const;
    Mat4 proj() const;
    Mat4 iview() const;
    Mat4 iproj() const;

    void click(u8 button);
    void unclick(u8 button);
    void mouse(Vec2 off);
    void scroll(f32 off);
    void move(bool f, bool b, bool l, bool r, bool u, bool d, f32 dt);

    void ar(u64 w, u64 h);
    void ar(Vec2 dim);
    void look_at(Vec3 pos, Vec3 at);

    Orbit orbit() const;
    First first() const;
    bool is_grabbed() const;
    f32 near_dist() const;

    void set_speed(f32 speed);
    void set_pos(Vec3 p);
    Vec3 pos() const;

    void gui();

private:
    void cache();
    Tuple<Vec3, Vec3, Vec3> axes() const;

    f32 vert_fov = 90.0f, aspect_ratio = 1.777f, near_plane = 0.01f;

    Window& window;
    Vec2 grab_pos;
    bool grabbed = false;

    Mode mode_ = Mode::orbit;
    Orbit orbit_;
    First first_;
    Mat4 view_, proj_, iview_, iproj_;
};

RPP_ENUM(Camera::Mode, orbit, RPP_CASE(orbit), RPP_CASE(first));
