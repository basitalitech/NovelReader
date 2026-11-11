#pragma once
#include <cstddef>
struct vita2d_color_vertex { float x,y,z; unsigned color; };
struct vita2d_texture_vertex { float x,y,z,u,v; };
enum SceGxmPrimitiveType { SCE_GXM_PRIMITIVE_TRIANGLES };
inline bool& test_pool_failure(){static bool value=false;return value;}
inline unsigned& test_pool_bytes(){static unsigned value=0;return value;}
inline void* vita2d_pool_memalign(unsigned size,unsigned) {
    test_pool_bytes()=size;
    alignas(16) static unsigned char storage[524288];
    return !test_pool_failure() && size<=sizeof(storage)?storage:nullptr;
}
using DrawArrayObserver=void (*)(const vita2d_color_vertex*,std::size_t);
inline DrawArrayObserver& draw_array_observer(){static DrawArrayObserver observer=nullptr;return observer;}
inline void vita2d_draw_array(SceGxmPrimitiveType,const vita2d_color_vertex* vertices,std::size_t count) {
    if(draw_array_observer())draw_array_observer()(vertices,count);
}
struct vita2d_font {};
struct vita2d_pgf {};
struct vita2d_texture {};
constexpr int SCE_GXM_TEXTURE_FORMAT_A8B8G8R8=0;
constexpr int SCE_GXM_TEXTURE_FILTER_LINEAR=0;
template<class... T> void vita2d_texture_set_filters(T...){}
inline bool& test_render_target_failure(){static bool value=false;return value;}
inline unsigned& test_texture_parts(){static unsigned value=0;return value;}
inline unsigned& test_fullscreen_clips(){static unsigned value=0;return value;}
inline unsigned& test_textured_batches(){static unsigned value=0;return value;}
inline void vita2d_draw_array_textured(const vita2d_texture*,SceGxmPrimitiveType,const vita2d_texture_vertex*,std::size_t,unsigned){
    ++test_texture_parts();++test_textured_batches();
}
inline vita2d_texture* vita2d_create_empty_texture_rendertarget(unsigned,unsigned,int){
    static vita2d_texture texture;return test_render_target_failure()?nullptr:&texture;
}
template<class... T> void vita2d_start_drawing_advanced(T...){}
template<class... T> void vita2d_draw_texture_part(T...){++test_texture_parts();}
template<class... T> void vita2d_draw_texture_tint_part_scale(T...){++test_texture_parts();}
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
inline void vita2d_set_clip_rectangle(int left,int top,int right,int bottom){
    if(left==0 && top==0 && right==960 && bottom==544)++test_fullscreen_clips();
}
