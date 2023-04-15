#include <iostream>
#include <cstdio>
#include <cassert>
#include "slab.h"
using namespace std;

__attribute__((aligned(PAGE_SIZE))) uint8 g_mem[MEM_SIZE];
void *g_page_free_list;
meminfo_t g_meminfo;
cache_t *g_caches; // obj_size 2^5~2^9

void *alloc_one_page()
{
    void *ret = g_page_free_list;
    g_page_free_list = *(void **)g_page_free_list;
    assert((uint8 *)g_page_free_list - g_mem < MEM_SIZE);
    if (g_meminfo.allocated < MEM_SIZE)
    {
        g_meminfo.allocated += PAGE_SIZE;
        g_meminfo.free -= PAGE_SIZE;
    }
    else
    {
        return 0;
    }
    assert(g_meminfo.allocated <= MEM_SIZE);
    assert(g_meminfo.free >= 0);
    g_meminfo.page_alloc_cnt++;
    return ret;
}

void free_one_page(void *addr)
{
    assert(g_meminfo.free >= 0);
    assert(g_meminfo.allocated <= MEM_SIZE);
    assert((ull)addr % PAGE_SIZE == 0);
    void *old_hd = g_page_free_list;
    g_page_free_list = addr;
    *(void **)g_page_free_list = old_hd;
    g_meminfo.allocated -= PAGE_SIZE;
    g_meminfo.free += PAGE_SIZE;
    g_meminfo.page_free_cnt++;
}

slab_t *alloc_one_slab(uint32 obj_size, cache_t *parent_cache) // init
{
    slab_t *p_slab = (slab_t *)alloc_one_page();
    if (!p_slab)
        return 0;
    p_slab->prev = p_slab->next = p_slab;
    p_slab->cache_ptr = parent_cache;
    p_slab->bitmap = (uint32 *)(p_slab + 1);
    p_slab->obj_num_limit = p_slab->obj_num = 0;
    uint32 space = PAGE_SIZE - sizeof(slab_t);
    while (space > obj_size + sizeof(uint32))
    {
        space -= (obj_size + sizeof(uint32));
        p_slab->bitmap[p_slab->obj_num_limit++] = 0;
    }
    // set obj arr
    p_slab->obj_arr = (void *)&p_slab->bitmap[p_slab->obj_num_limit];
    // set obj list
    uint32 i;
    for (i = 0, p_slab->obj_list_hd = p_slab->obj_arr; i + 1 < p_slab->obj_num_limit; i++)
    {
        *(void **)((uint8 *)p_slab->obj_arr + obj_size * i) = (void *)((uint8 *)p_slab->obj_arr + obj_size * (i + 1));
    }
    *(void **)((uint8 *)p_slab->obj_arr + obj_size * i) = 0;

    if (p_slab->cache_ptr)
    {
        p_slab->cache_ptr->slab_num++;

        // link new slab to cache
        if (!parent_cache->slab_list)
        {
            parent_cache->slab_list = p_slab;
#ifdef DDEBUG
            cout << "no slab, alloc obj " << ret << " on new slab" << slab_ptr << ", new obj idx " << idx << endl;
#endif
        }
        else
        {
            p_slab->prev = parent_cache->slab_list->prev;
            p_slab->next = parent_cache->slab_list;
            p_slab->prev->next = p_slab;
            p_slab->next->prev = p_slab;
#ifdef DDEBUG
            cout << "all slabs are full, alloc obj " << ret << " on new slab " << slab_ptr << ", new obj idx " << idx << endl;
#endif
        }
    }

    return p_slab;
}

void init_g_caches()
{
    g_meminfo.allocated = g_meminfo.page_alloc_cnt = g_meminfo.page_free_cnt = 0;
    g_meminfo.free = MEM_SIZE;
    g_meminfo.start_order = 5;
    g_meminfo.end_order = 10;

    int obj_size = 1;
    while (obj_size < sizeof(cache_t))
        obj_size <<= 1;

    g_meminfo.all_caches = alloc_one_slab(obj_size, 0);
    g_caches = (cache_t *)g_meminfo.all_caches->obj_arr;

    int i;
    for (i = g_meminfo.start_order; i < g_meminfo.end_order; i++)
    {
        cache_t *cc = &g_caches[i - g_meminfo.start_order];
        cc->obj_size = 1 << i;
        cc->slab_list = 0;
        cc->slab_num = 0;
    }
}

void init_g_page_free_list()
{
    uint8 *p = g_mem;
    g_page_free_list = (void *)p;
    while (p - (uint8 *)g_page_free_list < MEM_SIZE)
    {
        uint8 *nxt = p + PAGE_SIZE;
        *(void **)p = (void *)nxt;
        p = nxt;
    }
}

void *alloc_obj(uint32 size) // 2^5~2^9
{                            // check size
    int i;
    cache_t *pcache = 0;
    slab_t *slab_ptr=0;
    void *ret = 0;
    for (i = 0; i < g_meminfo.end_order - g_meminfo.start_order; i++)
    {
        if (size == g_caches[i].obj_size)
        {
            pcache = &g_caches[i];
            break;
        }
    }
    if (!pcache)
    {
        cout << "obj size must be 2^5~2^9, actually it is " << size << endl;
        return 0;
    }
    assert(pcache);

    // 1. try to allocte from existing slab
    if (pcache->slab_list)
    {
        slab_t *tmp = pcache->slab_list;
        do
        {
            if (tmp->obj_num < tmp->obj_num_limit)
            {

                ull idx = ((ull)tmp->obj_list_hd - (ull)tmp->obj_arr) / size;
                assert(tmp->obj_list_hd >= tmp->obj_arr);
                assert(((ull)tmp->obj_list_hd - (ull)tmp->obj_arr) % size == 0);
                assert(!tmp->bitmap[idx]);

                ret = tmp->obj_list_hd;
                tmp->obj_list_hd = *(void **)tmp->obj_list_hd;

                tmp->bitmap[idx] = 1;
                tmp->obj_num++;

#ifdef DDEBUG
                cout << "alloc obj " << ret << " on partial slab " << tmp << ", new obj idx " << idx << endl;
#endif
                return ret;
            }
            tmp = tmp->next;
        } while (tmp != pcache->slab_list);
    }

    // 2. all slabs are full || no slab, need to create new slab

    // alloc and init a slab
    slab_ptr = alloc_one_slab(size, pcache);

    if (!slab_ptr)
    {
        cout << "alloc_one_slab return null" << endl;
        return 0;
    }

    // alloc a obj
    ull idx = ((ull)slab_ptr->obj_list_hd - (ull)slab_ptr->obj_arr) / size;
    assert(slab_ptr->obj_list_hd >= slab_ptr->obj_arr);
    assert(((ull)slab_ptr->obj_list_hd - (ull)slab_ptr->obj_arr) % size == 0);
    assert(!slab_ptr->bitmap[idx]);

    ret = slab_ptr->obj_list_hd;
    slab_ptr->obj_list_hd = *(void **)slab_ptr->obj_list_hd;
    slab_ptr->bitmap[idx] = 1;
    slab_ptr->obj_num++;
    return ret;
}

void free_one_slab(slab_t *p_slab)
{
    assert(!p_slab->obj_num);
    assert(p_slab->cache_ptr);

    if (p_slab->next != p_slab)
    {
        p_slab->next->prev = p_slab->prev;
        p_slab->prev->next = p_slab->next;
        p_slab->prev = p_slab->next = p_slab;
    }
    else
    {
        assert(p_slab->cache_ptr);
        p_slab->cache_ptr->slab_list = 0;
    }

    p_slab->cache_ptr->slab_num--;
    free_one_page(p_slab);
}

void free_obj(void *addr)
{
    uint32 i;
    for (i = 0; i < g_meminfo.end_order - g_meminfo.start_order; i++)
    {
        slab_t *slab_ptr = g_caches[i].slab_list;
        if (!slab_ptr)
            continue;
        do
        {
            void *st = slab_ptr->obj_arr;
            void *ed = (uint8 *)slab_ptr->obj_arr + g_caches[i].obj_size * slab_ptr->obj_num;
            if (addr < ed && addr >= st)
            {
                ull idx = ((ull)addr - (ull)slab_ptr->obj_arr) / g_caches[i].obj_size;
                assert(((ull)addr - (ull)slab_ptr->obj_arr) % g_caches[i].obj_size == 0);
                assert(slab_ptr->bitmap[idx]);

                slab_ptr->bitmap[idx] = 0;

                slab_ptr->obj_num--;

                *(void **)addr = slab_ptr->obj_list_hd;
                slab_ptr->obj_list_hd = addr;
#ifdef DDEBUG
                cout << "free on slab " << slab_ptr << " obj " << addr << ", idx " << idx << endl;
#endif

                if (slab_ptr->obj_num == 0)
                {
                    free_one_slab(slab_ptr);
                }
                break;
            }
            slab_ptr = slab_ptr->next;
        } while (slab_ptr != g_caches[i].slab_list);
    }
}

void print_g_meminfo()
{
    int tot_slab = 0;
    int i;
    for (i = 0; i < g_meminfo.end_order - g_meminfo.start_order; i++)
    {
        tot_slab += g_caches[i].slab_num;
    }
    cout << " g_meminfo.allocated " << g_meminfo.allocated
         << " ,g_meminfo.free " << g_meminfo.free
         << " ,g_meminfo free rate " << 1.0 * g_meminfo.free / (g_meminfo.allocated + g_meminfo.free) * 100
         << " ,alloc page cnt " << g_meminfo.page_alloc_cnt
         << " ,free page cnt " << g_meminfo.page_free_cnt
         << " ,tot slab " << tot_slab
         //  << ",(g_meminfo.page_alloc_cnt- g_meminfo.page_free_cnt)*PAGE_SIZE " << (g_meminfo.page_alloc_cnt - g_meminfo.page_free_cnt) * PAGE_SIZE
         << endl;
}

void check_null_in_obj_list_hd(){

}

void test()
{
    void *ret = 0;
    void **obj_stack = (void **)alloc_one_page();
    int alloc_num = 0;
    int a = 0;
    int b = 0;
    while (1)
    {
        int op = rand() & 1;
        if (op)
        {
            ret = alloc_obj(1 << 8);
            obj_stack[alloc_num++] = ret;
            a++;
        }
        else
        {
            if (alloc_num == 0)
                continue;
            b++;
            free_obj(obj_stack[--alloc_num]);
        }
#ifdef DDEBUG
        cout << "test alloc nums " << a
             << ",test free nums " << b
             << ",test obj_stack size " << alloc_num;
        print_g_meminfo();
#endif

        assert((g_meminfo.page_alloc_cnt - g_meminfo.page_free_cnt) * PAGE_SIZE == g_meminfo.allocated);
        assert(g_meminfo.allocated + g_meminfo.free == MEM_SIZE);
    }
}

int main()
{
    srand(time(NULL));
    init_g_page_free_list();
    init_g_caches();
    test();
    return 0;
}