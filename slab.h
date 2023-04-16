#define MEM_SIZE (1 << 20)
#define PAGE_SIZE (1 << 12)
#define ARRAY_SIZE(arr) (sizeof(arr) / sizeof(arr[0]))

typedef unsigned int uint32;
typedef unsigned short uint16;
typedef unsigned char uint8;
typedef unsigned long long ull;

struct cache_s;
struct slab_s;
struct meminfo_s;

typedef struct cache_s
{
    struct slab_s *slab_list;
    int obj_size; // 2^5~2^9
    ull allocated;
    ull free;
    int slab_num;
    // slab_t *free_slab_list;
    // slab_t *partial_slab_list;
    // slab_t *full_slab_list;
} cache_t;

typedef struct slab_s
{
    struct slab_s *prev;
    struct slab_s *next;
    cache_t *cache_ptr;
    int *bitmap; // 4 byte, for align
    int obj_num;
    int obj_num_limit;
    void *obj_arr;
    void *obj_list_hd;
} slab_t;

typedef struct meminfo_s
{
    struct slab_s *all_caches;
    int allocated;
    int free;
    int start_order;
    int end_order;
    int page_alloc_cnt;
    int page_free_cnt;
} meminfo_t;

#define CHECK_SLAB(p_slab)                                                                         \
    do                                                                                             \
    {                                                                                              \
        assert(p_slab);                                                                            \
        assert(p_slab->obj_num >= 0);                                                              \
        assert(p_slab->obj_num_limit > 0);                                                         \
        assert(p_slab->obj_num <= p_slab->obj_num_limit);                                          \
        assert(p_slab->cache_ptr);                                                                 \
        assert(p_slab->obj_arr);                                                                   \
        assert(p_slab->prev);                                                                      \
        assert(p_slab->next);                                                                      \
        assert(p_slab->next->prev);                                                                \
        assert(p_slab->prev->next);                                                                \
        assert(p_slab->next->prev == p_slab);                                                      \
        assert(p_slab->prev->next == p_slab);                                                      \
        assert(p_slab->obj_list_hd || (p_slab->obj_num == p_slab->obj_num_limit));                 \
        assert((ull)p_slab->bitmap == (ull)p_slab + sizeof(slab_t));                               \
        assert((ull)p_slab->bitmap + p_slab->obj_num_limit * sizeof(int) == (ull)p_slab->obj_arr); \
        int cnt = 0;                                                                               \
        void *obj = p_slab->obj_list_hd;                                                           \
        while (obj)                                                                                \
        {                                                                                          \
            obj = *(void **)obj;                                                                   \
            cnt++;                                                                                 \
        }                                                                                          \
        assert(cnt + p_slab->obj_num == p_slab->obj_num_limit);                                    \
    } while (0);
