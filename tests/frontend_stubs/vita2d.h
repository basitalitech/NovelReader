#pragma once
#include <cstddef>
struct vita2d_color_vertex { float x,y,z; unsigned color; };
enum SceGxmPrimitiveType { SCE_GXM_PRIMITIVE_TRIANGLES };
inline void* vita2d_pool_memalign(unsigned size,unsigned) {
    alignas(16) static unsigned char storage[16384];
    return size<=sizeof(storage)?storage:nullptr;
}
using DrawArrayObserver=void (*)(const vita2d_color_vertex*,std::size_t);
inline DrawArrayObserver& draw_array_observer(){static DrawArrayObserver observer=nullptr;return observer;}
inline void vita2d_draw_array(SceGxmPrimitiveType,const vita2d_color_vertex* vertices,std::size_t count) {
    if(draw_array_observer())draw_array_observer()(vertices,count);
}
struct vita2d_font {};
struct vita2d_pgf {};
struct vita2d_texture {};
inline vita2d_texture* vita2d_load_PNG_file(const char*){return nullptr;}
inline void vita2d_free_texture(vita2d_texture*){}
inline int vita2d_texture_get_width(const vita2d_texture*){return 100;}
inline int vita2d_texture_get_height(const vita2d_texture*){return 160;}
template<class... T> void vita2d_set_clear_color(T...){}
template<class... T> void vita2d_start_drawing(T...){}
template<class... T> void vita2d_clear_screen(T...){}
template<class... T> void vita2d_end_drawing(T...){}
template<class... T> void vita2d_common_dialog_update(T...){}
template<class... T> void vita2d_swap_buffers(T...){}
template<class... T> void vita2d_wait_rendering_done(T...){}
template<class... T> void vita2d_draw_rectangle(T...){}
template<class... T> void vita2d_draw_line(T...){}
template<class... T> void vita2d_draw_fill_circle(T...){}
template<class... T> void vita2d_draw_texture_scale(T...){}
template<class... T> void vita2d_draw_texture_tint(T...){}
template<class... T> void vita2d_draw_texture_tint_scale(T...){}
template<class... T> void vita2d_enable_clipping(T...){}
template<class... T> void vita2d_disable_clipping(T...){}
template<class... T> void vita2d_set_clip_rectangle(T...){}
