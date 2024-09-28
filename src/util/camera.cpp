
#include <SDL2/SDL_events.h>

#include "../gui/imgui_ext.h"
#include "camera.h"

const Vec3 UP = Vec3{0.0f, 1.0f, 0.0f};
const Vec3 FWD = Vec3{0.0f, 0.0f, -1.0f};

Camera::Camera(Window& window) : window(window) {
}

Mat4 Camera::view() const {
    return view_;
}

Mat4 Camera::proj() const {
    return proj_;
}

Mat4 Camera::iview() const {
    return iview_;
}

Mat4 Camera::iproj() const {
    return iproj_;
}

f32 Camera::near_dist() const {
    return near_plane;
}

Camera::First Camera::first() const {
    return first_;
}

Camera::Orbit Camera::orbit() const {
    return orbit_;
}

void Camera::look_at(Vec3 pos, Vec3 at) {

    switch(mode_) {
    case Mode::orbit: {
        Vec3 front = (at - pos).unit();
        orbit_.at = at;
        orbit_.radius = (at - pos).norm();
        if(Math::dot(front, UP) == -1.0f)
            orbit_.rot = Quat::euler(Vec3{270.0f, 0.0f, 0.0f});
        else
            orbit_.rot = Quat::euler(Mat4::rotate_z_to(front).to_euler());
    } break;
    case Mode::first: {
        Vec3 front = (at - pos).unit();
        first_.pos = pos;
        first_.rot = Quat::euler(Mat4::rotate_z_to(front).to_euler());
    } break;
    }

    cache();
}

bool Camera::is_grabbed() const {
    return grabbed;
}

void Camera::click(Uint8 button) {

    if(mode_ == Mode::orbit) {
        if(button == SDL_BUTTON_RIGHT) {
            orbit_.control = Orbit::Control::move;
        } else if(button == SDL_BUTTON_MIDDLE) {
            orbit_.control = Orbit::Control::orbit;
        } else if(button == SDL_BUTTON_LEFT) {
            auto [up, front, right] = axes();
            mode_ = Mode::first;
            look_at(orbit_.at - orbit_.radius * front, orbit_.at);
            grab_pos = window.begin_relative();
            grabbed = true;
        }
    }
}

void Camera::unclick(Uint8 button) {

    if(mode_ == Mode::orbit) {
        if((button == SDL_BUTTON_MIDDLE && orbit_.control == Orbit::Control::orbit) ||
           (button == SDL_BUTTON_RIGHT && orbit_.control == Orbit::Control::move)) {
            orbit_.control = Orbit::Control::none;
        }

    } else if(mode_ == Mode::first) {

        if(button == SDL_BUTTON_LEFT) {
            auto [up, front, right] = axes();
            mode_ = Mode::orbit;
            look_at(first_.pos, first_.pos + front * orbit_.radius);
            window.end_relative(grab_pos);
            grabbed = false;
        }
    }

    cache();
}

Vec3 Camera::pos() const {
    if(mode_ == Mode::first) return first_.pos;
    if(mode_ == Mode::orbit) {
        Vec3 front = orbit_.rot.rotate(FWD);
        return orbit_.at - orbit_.radius * front;
    }
    RPP_UNREACHABLE;
}

void Camera::set_speed(f32 speed) {
    first_.speed = speed;
}

void Camera::set_pos(Vec3 p) {
    if(mode_ == Mode::first) first_.pos = p;
    if(mode_ == Mode::orbit) {
        Vec3 front = orbit_.rot.rotate(FWD);
        orbit_.at = p + orbit_.radius * front;
    }
    cache();
}

Tuple<Vec3, Vec3, Vec3> Camera::axes() const {
    Quat rot;
    if(mode_ == Mode::orbit)
        rot = orbit_.rot;
    else if(mode_ == Mode::first)
        rot = first_.rot;

    Vec3 up = rot.rotate(UP);
    Vec3 front = rot.rotate(FWD);
    Vec3 right = Math::cross(front, up).unit();
    return Tuple<Vec3, Vec3, Vec3>{up, front, right};
}

void Camera::mouse(Vec2 off) {

    auto [up, front, right] = axes();

    if(mode_ == Mode::orbit) {

        if(orbit_.control == Orbit::Control::orbit) {
            f32 up_rot = -off.x * orbit_.ob_sens;
            f32 right_rot = off.y * orbit_.ob_sens;
            orbit_.rot =
                Quat::axis_angle(UP, up_rot) * Quat::axis_angle(right, right_rot) * orbit_.rot;
        } else if(orbit_.control == Orbit::Control::move) {
            orbit_.at += right * -off.x * orbit_.mv_sens + up * off.y * orbit_.mv_sens;
        }

    } else if(mode_ == Mode::first) {

        f32 up_rot = -off.x * first_.v_sens;
        f32 right_rot = -off.y * first_.v_sens;
        first_.rot = Quat::axis_angle(UP, up_rot) * Quat::axis_angle(right, right_rot) * first_.rot;
    }

    cache();
}

void Camera::scroll(f32 off) {
    if(mode_ == Mode::orbit) {
        orbit_.radius -= off * orbit_.rd_sens;
        orbit_.radius = Math::max(orbit_.radius, 2.0f * near_plane);
    }
    cache();
}

void Camera::move(bool f, bool b, bool l, bool r, bool u, bool d, f32 dt) {
    if(mode_ == Mode::first) {
        Vec3 up = first_.rot.rotate(UP);
        Vec3 front = first_.rot.rotate(FWD);
        Vec3 right = Math::cross(front, up).unit();
        if(f) first_.pos += front * dt * first_.speed;
        if(b) first_.pos -= front * dt * first_.speed;
        if(r) first_.pos += right * dt * first_.speed;
        if(l) first_.pos -= right * dt * first_.speed;
        if(u) first_.pos += UP * dt * first_.speed;
        if(d) first_.pos -= UP * dt * first_.speed;
    }
    cache();
}

void Camera::ar(u64 w, u64 h) {
    aspect_ratio = static_cast<f32>(w) / static_cast<f32>(h);
    cache();
}

void Camera::ar(Vec2 dim) {
    aspect_ratio = dim.x / dim.y;
    cache();
}

void Camera::cache() {

    Vec3 pos;
    Quat rot;
    if(mode_ == Mode::orbit) {
        Vec3 front = orbit_.rot.rotate(FWD);
        pos = orbit_.at - orbit_.radius * front;
        rot = orbit_.rot;
    } else if(mode_ == Mode::first) {
        pos = first_.pos;
        rot = first_.rot;
    }

    iview_ = Mat4::translate(pos) * rot.to_mat();
    proj_ = Mat4::proj(vert_fov, aspect_ratio, near_plane);
    view_ = iview_.inverse();
    iproj_ = proj_.inverse();
}

void Camera::gui() {
    using namespace ImGui;

    Combo("Mode", mode_);

    SliderFloat("FOV", &vert_fov, 1.0f, 179.0f);
    SliderFloat("AR", &aspect_ratio, 0.1f, 10.0f);
    DragFloat("Near", &near_plane, 0.01f, 0.001f, 1.0f, "%.3f");
    if(TreeNode("Orbit")) {
        DragFloat3("At", orbit_.at.data, 0.1f);
        SliderFloat4("Rot", orbit_.rot.data, 0.0f, 1.0f);
        orbit_.rot = orbit_.rot.unit();
        DragFloat("R", &orbit_.radius, 0.1f);
        SliderFloat("SensOrbit", &orbit_.ob_sens, 0.0f, 1.0f);
        SliderFloat("SensMv", &orbit_.mv_sens, 0.0f, 1.0f);
        SliderFloat("SensRd", &orbit_.rd_sens, 0.0f, 1.0f);
        TreePop();
    }
    if(TreeNode("First")) {
        DragFloat3("Pos", first_.pos.data, 0.1f);
        SliderFloat4("Rot", first_.rot.data, 0.0f, 1.0f);
        first_.rot = first_.rot.unit();
        SliderFloat("SensV", &first_.v_sens, 0.0f, 1.0f);
        DragFloat("Speed", &first_.speed, 0.1f);
        TreePop();
    }
}
