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
    uint32 obj_size; // 2^5~2^9
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
    uint32 *bitmap; // 4 byte, for align
    uint32 obj_num;
    uint32 obj_num_limit;
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
