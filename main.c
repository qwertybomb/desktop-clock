#if defined(_MSC_VER) && !defined(_CRT_SECURE_NO_WARNINGS )
#define _CRT_SECURE_NO_WARNINGS
#endif

// standard headers
#include <omp.h>
#include <math.h>
#include <time.h>
#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <immintrin.h>

// opengl headers
#include <GL/glew.h>

// SDL2 headers
#include <SDL2/SDL.h>

#ifndef DEBUG_MODE
#define DEBUG_MODE (0)
#endif

#if DEBUG_MODE >= 1
#define NDEBUG
#endif

#ifdef _MSC_VER
#define _Alignas(x) __declspec(align(x))
#endif

// external headers (these are not mine)
#include "simdxorshift128plus.c"
#define ARCBALL_CAMERA_IMPLEMENTATION
#include "arcball_camera.h"

// project headers
#include "shaders.h"

#define MAX_ITERATIONS (10)
#define BAILOUT_NORM (4.0f)
#define IMAGE_WIDTH (512)
#define IMAGE_HEIGHT (512)
#define IMAGE_DEPTH (512)
#define TOTAL_SAMPLES (700000000)

typedef uint32_t heatmap_t;
typedef heatmap_t (*heatmap_image_t)[IMAGE_HEIGHT][IMAGE_WIDTH];
typedef uint8_t (*heatmap_image_uint8_t)[IMAGE_HEIGHT][IMAGE_WIDTH];

static __m256 random_v8sf(avx_xorshift128plus_key_t *const key)
{
    __m256i const as_int = avx_xorshift128plus(key);
    __m256 const as_float = _mm256_cvtepi32_ps(as_int);
    return  _mm256_mul_ps(as_float, _mm256_set1_ps(1.0f / (1u << 31) * 2.0f));
}

static bool v8si_lt(__m256i a, __m256i b)
{
    __m256i const pcmp = _mm256_cmpgt_epi32(b, a);
    int const bitmask = _mm256_movemask_epi8(pcmp);
    return bitmask != 0;
}

static __m256i mandelbulb_set(__m256 const cx,
                              __m256 const cy,
                              __m256 const cz,
                              __m256(*const orbit)[3])
{
    __m256 zx = cx;
    __m256 zy = cy;
    __m256 zz = cz;
    __m256i iterations = _mm256_setzero_si256();

    for(int i = 0; i < MAX_ITERATIONS; ++i)
    {
        // test if we have done enough iterations
        __m256i const pcmp = _mm256_cmpeq_epi32(_mm256_set1_epi32(MAX_ITERATIONS), iterations);
        int const bitmask = _mm256_movemask_epi8(pcmp);
        if (bitmask != 0u)
        {
            break;
        }

        //  from https://www.iquilezles.org/www/articles/mandelbulb/mandelbulb.htm
        __m256 const x = zx;
        __m256 const x2 = _mm256_mul_ps(x, x);
        __m256 const x4 = _mm256_mul_ps(x2, x2);

        __m256 const y = zy;
        __m256 const y2 = _mm256_mul_ps(y, y);
        __m256 const y4 = _mm256_mul_ps(y2, y2);

        __m256 const z = zz;
        __m256 const z2 = _mm256_mul_ps(z, z);
        __m256 z4 = _mm256_mul_ps(z2, z2);

        __m256 const k3 = _mm256_add_ps(x2, z2);
        __m256 const k2 = _mm256_rsqrt_ps(
            _mm256_mul_ps(
                _mm256_mul_ps(
                    _mm256_mul_ps(_mm256_mul_ps(k3, k3), k3),
                    _mm256_mul_ps(_mm256_mul_ps(k3, k3), k3)), k3));

        __m256 const k1 =
            _mm256_fmadd_ps(
                _mm256_mul_ps(_mm256_set1_ps(2.0f), z2), x2,
                _mm256_sub_ps(
                    _mm256_sub_ps(
                        _mm256_add_ps(_mm256_add_ps(x4, y4), z4),
                        _mm256_mul_ps(_mm256_mul_ps(_mm256_set1_ps(6.0f), y2), z2)),
                    _mm256_mul_ps(_mm256_mul_ps(_mm256_set1_ps(6.0f), x2), y2)));
        __m256 const k4 = _mm256_add_ps(_mm256_sub_ps(x2, y2), z2);

        zx =
            _mm256_mul_ps(
                _mm256_mul_ps(
                    _mm256_mul_ps(
                        _mm256_mul_ps(
                            _mm256_mul_ps(
                                _mm256_mul_ps(
                            _mm256_mul_ps(
                                _mm256_set1_ps(64.0f), x), y), z),
                            _mm256_sub_ps(x2, z2)), k4),
                    _mm256_add_ps(
                        _mm256_fmadd_ps(
                            _mm256_mul_ps(
                                _mm256_set1_ps(-6.0f), x2), z2, z4), x4)),
                _mm256_mul_ps(k1, k2));

        zy =
            _mm256_fmadd_ps(
                _mm256_mul_ps(
                    _mm256_mul_ps(_mm256_set1_ps(-16.0f), y2),
                    _mm256_mul_ps(k3, k4)), k4,
                _mm256_mul_ps(k1, k1));

        zz =
            _mm256_mul_ps(
                _mm256_mul_ps(
                    _mm256_mul_ps(
                        _mm256_mul_ps(
                            _mm256_set1_ps(-8.0f), y), k4),
                    _mm256_fmadd_ps(
                        z4, z4,
                        _mm256_fmadd_ps(
                            _mm256_mul_ps(
                                _mm256_mul_ps(
                                    _mm256_set1_ps(-28.0f), x2), z2), z4,
                            _mm256_fmadd_ps(
                                _mm256_mul_ps(_mm256_set1_ps(70.0f), x4), z4,
                                _mm256_fmadd_ps(
                                    _mm256_mul_ps(
                                        _mm256_mul_ps(
                                            _mm256_set1_ps(-28.0f), x4), x2), z2,
                                    _mm256_mul_ps(x4, x4)))))),
                _mm256_mul_ps(k1, k2));

        zx = _mm256_add_ps(zx, cx);
        zy = _mm256_add_ps(zy, cy);
        zz = _mm256_add_ps(zz, cz);

        orbit[i][0] = zx;
        orbit[i][1] = zy;
        orbit[i][2] = zz;

        // test to see if any of the points are in bounds
        __m256i const bounded =
            _mm256_castps_si256(
                _mm256_cmp_ps(
                    _mm256_fmadd_ps(zx, zx, _mm256_fmadd_ps(zy, zy, _mm256_mul_ps(zz, zz))),
                    _mm256_set1_ps(BAILOUT_NORM), _CMP_LT_OS));

        if (_mm256_testz_si256(bounded, bounded))
        {
            break;
        }

        iterations = _mm256_sub_epi32(iterations, bounded);
    }

    // if a point did not escape we don't want to plot
    return _mm256_andnot_si256(_mm256_cmpeq_epi32(
                                   iterations,
                                   _mm256_set1_epi32(MAX_ITERATIONS)),
                               iterations);
}

static __m256 map_range(__m256 const input,
                        __m256 const in_min,
                        __m256 const in_max,
                        __m256 const out_min,
                        __m256 const out_max)
{
    return
        _mm256_fmadd_ps(
            _mm256_div_ps(
                _mm256_sub_ps(out_max, out_min),
                _mm256_sub_ps(in_max, in_min)),
            _mm256_sub_ps(input, in_min), out_min);
}

static void generate_heatmap(heatmap_image_t const restrict heatmap,
                             heatmap_t *const restrict max_value,
                             int const individual_samples,
                             int const id)
{
    avx_xorshift128plus_key_t rng_key;
    avx_xorshift128plus_init(324, 4444, &rng_key);

    // make sure different threads start out differently
    for(int i = 0; i < id; ++i)
    {
        avx_xorshift128plus_jump(&rng_key);
    }

    for(int i = 0; i < individual_samples; ++i)
    {
        __m256 const cx = random_v8sf(&rng_key);
        __m256 const cy = random_v8sf(&rng_key);
        __m256 const cz = random_v8sf(&rng_key);

        int j = 0;
        _Alignas(32) __m256 orbit[MAX_ITERATIONS][3] = {0};

        __m256i const iterations = mandelbulb_set(cx, cy, cz, orbit);
        for(__m256i i = _mm256_setzero_si256();
            v8si_lt(i, iterations);
            i = _mm256_add_epi32(i, _mm256_set1_epi32(1)), ++j)
        {
            __m256i const x = _mm256_cvtps_epi32(map_range(orbit[j][0],
                                                           _mm256_set1_ps(-2.0f),
                                                           _mm256_set1_ps(+2.0f),
                                                           _mm256_set1_ps(0),
                                                           _mm256_set1_ps(IMAGE_WIDTH - 1)));

            __m256i const y = _mm256_cvtps_epi32(map_range(orbit[j][1],
                                                           _mm256_set1_ps(-2.0f),
                                                           _mm256_set1_ps(+2.0f),
                                                           _mm256_set1_ps(0),
                                                           _mm256_set1_ps(IMAGE_HEIGHT - 1)));

            __m256i const z = _mm256_cvtps_epi32(map_range(orbit[j][2],
                                                           _mm256_set1_ps(-2.0f),
                                                           _mm256_set1_ps(+2.0f),
                                                           _mm256_set1_ps(0),
                                                           _mm256_set1_ps(IMAGE_DEPTH - 1)));

            for(int lane = 0; lane < 8; ++lane)
            {
                typedef union index_access
                {
                    __m256i v;
                    int32_t i[8];
                } index_access_t;

                int32_t const x_lane = (index_access_t){x}.i[lane];
                int32_t const y_lane = (index_access_t){y}.i[lane];
                int32_t const z_lane = (index_access_t){z}.i[lane];
                int32_t const i_lane = (index_access_t){i}.i[lane];
                int32_t const iterations_lane = (index_access_t){iterations}.i[lane];

                if (i_lane < iterations_lane &&
                    x_lane >= 0 && x_lane < IMAGE_WIDTH  &&
                    y_lane >= 0 && y_lane < IMAGE_HEIGHT &&
                    z_lane >= 0 && z_lane < IMAGE_DEPTH)
                {
                    heatmap_t const new_value = ++heatmap[z_lane][y_lane][x_lane];
                    *max_value = *max_value < new_value ? new_value : *max_value;
                }
            }
        }
    }
}

static void combine_heatmap(heatmap_image_t const restrict heatmap,
                            heatmap_t *const restrict max_value,
                            int const count)
{
    for (int z = IMAGE_DEPTH; z < IMAGE_DEPTH * count; ++z)
    {
        for (int y = IMAGE_HEIGHT; y < IMAGE_HEIGHT; ++y)
        {
            for (int x = 0; x < IMAGE_WIDTH; x += 8)
            {
                void *const dst = &heatmap[z % IMAGE_DEPTH][y][x];
                void const *const src = &heatmap[z][y][x];
                _mm256_store_si256(dst, _mm256_add_epi32(_mm256_load_si256(dst),
                                                         _mm256_load_si256(src)));
            }
        }
    }

    for (int i = 0; i < count; ++i)
    {
        max_value[0] = max_value[i] > max_value[0] ? max_value[i] : max_value[0];
    }
}

static heatmap_image_uint8_t transform_heatmap(heatmap_image_t const heatmap,
                                               heatmap_t const max_value)
{
    // find the min heatmap value
    heatmap_t min_value = ~(heatmap_t)0;
    for(int z = 0; z < IMAGE_DEPTH; ++z)
    {
        for(int y = 0; y < IMAGE_HEIGHT; ++y)
        {
            for(int x = 0; x < IMAGE_WIDTH; ++x)
            {
                heatmap_t const value = heatmap[z][y][x];
                min_value = min_value > value ? value : min_value;
            }
        }
    }

    float const max_value_float = (float)max_value;
    heatmap_image_uint8_t const heatmap_image = (heatmap_image_uint8_t)heatmap;

    for(int z = 0; z < IMAGE_DEPTH; ++z)
    {
        for(int y = 0; y < IMAGE_HEIGHT; ++y)
        {
            for(int x = 0; x < IMAGE_WIDTH; ++x)
            {
                heatmap_t const value = heatmap[z][y][x] - min_value;
                heatmap_image[z][y][x] = (uint8_t)(logf(value) / logf(6.0f) / max_value_float * 255.0f + 0.5f);
            }
        }
    }

    return heatmap_image;
}

static void *allocate_aligned(size_t const size, size_t const alignment)
{
    size_t const mask = alignment - 1;
    uintptr_t const mem = (uintptr_t) calloc(size + alignment, 1);
    return (void *) ((mem + mask) & ~mask);
}

static GLuint load_volume_texture(heatmap_image_uint8_t const image)
{
    GLuint texture;
    glGenTextures(1, &texture);

    glBindTexture(GL_TEXTURE_3D, texture);
    glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_BASE_LEVEL, 0);
    glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_WRAP_R, GL_CLAMP_TO_EDGE);

    glTexImage3D(GL_TEXTURE_3D, 0, GL_RED,
                 IMAGE_WIDTH, IMAGE_HEIGHT,
                 IMAGE_DEPTH, 0, GL_RED,
                 GL_UNSIGNED_BYTE, image);

    return texture;
}

static GLuint compile_shaders(void)
{
    GLuint const vertex_shader = glCreateShader(GL_VERTEX_SHADER);
    glShaderSource(vertex_shader, 1, &vertex_shader_source, NULL);
    glCompileShader(vertex_shader);

    GLuint const fragment_shader = glCreateShader(GL_FRAGMENT_SHADER);
    glShaderSource(fragment_shader, 1, &fragment_shader_source, NULL);
    glCompileShader(fragment_shader);

#if DEBUG_MODE >= 1
    GLint success;
    char info_log[512];
    glGetShaderiv(fragment_shader, GL_COMPILE_STATUS, &success);
    if(!success)
    {
        glGetShaderInfoLog(fragment_shader, 512, NULL, info_log);
        fputs(info_log, stdout);
    }
#endif

    GLuint const shader_program = glCreateProgram();

    glAttachShader(shader_program, vertex_shader);
    glAttachShader(shader_program, fragment_shader);
    glLinkProgram(shader_program);

    glUseProgram(shader_program);

    return shader_program;
}

static void bind_volume_texture(GLuint const shader_program,
                                GLuint const volume_texture)
{
    GLint const uniform_location = glGetUniformLocation(shader_program, "tex");

    glUniform1i(uniform_location, 0);

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_3D, volume_texture);
}

static heatmap_image_uint8_t generate_heatmap_image(char const *const filepath,
                                                    bool const force_compute)
{
    if (filepath != NULL && !force_compute)
    {
        FILE *const file = fopen(filepath, "rb");

        // if the file does not exist that means we are creating a new file
        if (file == NULL)
        {
            goto file_did_not_exist;
        }

        heatmap_image_uint8_t const heatmap_image =
            malloc(sizeof *heatmap_image * IMAGE_DEPTH);

        fread(heatmap_image, 1, sizeof *heatmap_image * IMAGE_DEPTH, file);

        fclose(file);

        return heatmap_image;
    }
    else
    {
        file_did_not_exist:;
        clock_t const start_clock = clock();

        int const max_threads = omp_get_max_threads();
        heatmap_image_t const heatmap =
            allocate_aligned(sizeof *heatmap * IMAGE_DEPTH * max_threads +
                             sizeof ***heatmap * max_threads , 32);

        heatmap_t *const max_value = &heatmap[IMAGE_DEPTH * max_threads][0][0];

        // generate the heatmap
#pragma omp parallel
        {
            int const id = omp_get_thread_num();
            generate_heatmap(&heatmap[IMAGE_DEPTH * id], &max_value[id],
                             TOTAL_SAMPLES / max_threads, id);
        }

        // combine it from different threads
        combine_heatmap(heatmap, max_value, max_threads);

        // transform the heatmap so it is now an image not a heatmap
        heatmap_image_uint8_t const heatmap_image = transform_heatmap(heatmap, max_value[0]);

        clock_t const end_clock = clock();
        printf("it took %f seconds\n", (double)(end_clock - start_clock) / CLOCKS_PER_SEC);

        if (filepath != NULL)
        {
            FILE *const file = fopen(filepath, "wb");
            fwrite(heatmap_image, 1, sizeof *heatmap_image * IMAGE_DEPTH, file);
            fclose(file);
        }


        return heatmap_image;
    }
}

int main(int const argc, char **const argv)
{
    (void)avx_xorshift128plus_shuffle32;

    bool force_compute = false;
    char const *cache_result_path = "3d.mandelbulb_heatmap_plot.3d.bin";
    switch(argc)
    {
        case 0:
        case 1: break;
        default:
        {
            for(int i = 1; i < argc; ++i)
            {
                if (strncmp(argv[i], "--cache-result", sizeof "--cache-result" - 1) == 0)
                {
	            char const *const value = argv[i] + sizeof "--cache-result" - 1;
                    if (value[0] == '=' && value[0] != value[1] != '\0')
                    {
                        cache_result_path = value + 1;
                    }
                    else if(value[0] == '=')
                    {
                        goto argument_error;
                    }
                }
                else if(strcmp(argv[i], "--no-cache") == 0)
                {
                    cache_result_path = NULL;
                }
                else if (strncmp(argv[i], "--force-compute", sizeof "--force-compute" - 1) == 0)
                {
                    char const *const value = argv[i] + sizeof "--force-compute" - 1;
                    if (value[0] == '=' && value[1] != '\0')
                    {
                        if(strcmp(value + 1, "true") == 0)
                        {
                            force_compute = true;
                        }
                        else if(strcmp(value + 1, "false") == 0)
                        {
                            force_compute = false;
                        }
                        else
                        {
                            goto argument_error;
                        }
                    }
                    else if (value[0] != '\0')
                    {
                        goto argument_error;
                    }
                    else
                    {
                        force_compute = true;
                    }
                }
                else
                {
                    argument_error:
                    fprintf(stderr, "%s is not a valid argument\n", argv[i]);
                    return EXIT_FAILURE;
                }
            }

            break;
        }
    }

    heatmap_image_uint8_t const heatmap_image =
        generate_heatmap_image(cache_result_path, force_compute);

    // init SDL
    SDL_Init(SDL_INIT_VIDEO | SDL_INIT_TIMER);

    // create a window
    int const window_width = 750;
    int const window_height = 750;
    SDL_Window *const window = SDL_CreateWindow("",
                                                SDL_WINDOWPOS_UNDEFINED,
                                                SDL_WINDOWPOS_UNDEFINED,
                                                window_width, window_height,
                                                SDL_WINDOW_OPENGL);

    SDL_GL_CreateContext(window);

    glewInit();

    GLuint const texture = load_volume_texture(heatmap_image);
    GLuint const shader = compile_shaders();

    bind_volume_texture(shader, texture);

    GLint const transform_uniform_location = glGetUniformLocation(shader, "transform");
    glUniform1i(glGetUniformLocation(shader, "depth_steps"), IMAGE_DEPTH);

    float pos[3] = { 0.0f, 0.0f, 1.5f };
    float target[3] = { 0.0f, 0.0f, 0.0f };

    // initialize "up" to be tangent to the sphere!
    // up = cross(cross(look, world_up), look)
    float up[3];
    {
        float look[3] = { target[0] - pos[0], target[1] - pos[1], target[2] - pos[2] };
        float look_len = sqrtf(look[0] * look[0] + look[1] * look[1] + look[2] * look[2]);
        look[0] /= look_len;
        look[1] /= look_len;
        look[2] /= look_len;

        float const world_up[3] = { 0.0f, 1.0f, 0.0f };

        float across[3] = {
                look[1] * world_up[2] - look[2] * world_up[1],
                look[2] * world_up[0] - look[0] * world_up[2],
                look[0] * world_up[1] - look[1] * world_up[0],
        };

        up[0] = across[1] * look[2] - across[2] * look[1];
        up[1] = across[2] * look[0] - across[0] * look[2];
        up[2] = across[0] * look[1] - across[1] * look[0];

        float up_len = sqrtf(up[0] * up[0] + up[1] * up[1] + up[2] * up[2]);
        up[0] /= up_len;
        up[1] /= up_len;
        up[2] /= up_len;
    }

    int old_cursor_x, old_cursor_y;
    SDL_GetMouseState(&old_cursor_x, &old_cursor_y);

    uint64_t old_time = SDL_GetPerformanceCounter();
    uint64_t const performance_frequency = SDL_GetPerformanceFrequency();

    for(;;)
    {
        SDL_PumpEvents();
        uint32_t const is_mouse_down = SDL_GetMouseState(NULL, NULL);
        bool const mouse_down[2] = {
                (is_mouse_down & SDL_BUTTON(SDL_BUTTON_MIDDLE)) != 0,
                (is_mouse_down & SDL_BUTTON(SDL_BUTTON_LEFT)) != 0
        };

        int mouse_wheel_delta = 0;
        for(SDL_Event event; SDL_PollEvent(&event) != 0; )
        {
            switch(event.type)
            {
                case SDL_QUIT:
                {
                    goto cleanup;
                }

                case SDL_MOUSEWHEEL:
                {
                    mouse_wheel_delta = event.wheel.y;
                    break;
                }
            }
        }

        // update the camera
        {
            int new_cursor_x, new_cursor_y;
            SDL_GetMouseState(&new_cursor_x, &new_cursor_y);

            uint64_t const new_time = SDL_GetPerformanceCounter();
            float const delta_time = (float)((double)(new_time - old_time) /
                                             (double)performance_frequency);

            float view[16];
            arcball_camera_update(pos, target, up, view,
                                  delta_time, 0.1f, 1.0f, 2.0f,
                                  window_width, window_height,
                                  old_cursor_x, new_cursor_x,
                                  old_cursor_y, new_cursor_y,
                                  mouse_down[0], mouse_down[1],
                                  mouse_wheel_delta, 0);

            old_cursor_x = new_cursor_x;
            old_cursor_y = new_cursor_y;
            old_time = new_time;

            glUniformMatrix4fv(transform_uniform_location, 1, GL_FALSE, view);
        }

        // draw to the screen
        {
            glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);

            SDL_GL_SwapWindow(window);
        }
    }

    cleanup:
    {
        SDL_GL_DeleteContext(window);
        SDL_DestroyWindow(window);
    }

    return 0;
}
