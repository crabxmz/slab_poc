#include <iostream>
#include <cstdio>
#include <cassert>
#include "slab.h"
using namespace std;

__attribute__((aligned(PAGE_SIZE))) uint8 g_mem[MEM_SIZE];
void *g_page_free_list;
meminfo_t g_meminfo;
cache_t *g_caches; // obj_size 2^5~2^9
int alloc_from = -1;
int op_from = -1;

#define GET_PAGE_IDX(ptr) ((ptr && (uint8 *)(ptr) >= g_mem && (uint8 *)(ptr) < g_mem + MEM_SIZE) ? (((uint8 *)(ptr)-g_mem) / PAGE_SIZE) : -1)

void print_cache(cache_t *cc)
{
    slab_t *p = cc->slab_list;
    int cnt = 0;
    if (!p)
        return;
    do
    {
        ++cnt;
        cout << "->|slab" << cnt << ' ' << p << "|";
        p = p->next;
    } while (p != cc->slab_list);
    cout << endl;

    if (cnt != cc->slab_num)
        cout << "slab link cnt " << cnt << " slab count num " << cc->slab_num << endl;
    assert(cnt == cc->slab_num);
}

void print_g_meminfo()
{
    int tot_slab = 0;
    int i;
    for (i = 0; i < g_meminfo.end_order - g_meminfo.start_order; i++)
    {
        tot_slab += g_caches[i].slab_num;
    }
    cout
        << "g_meminfo.allocated " << g_meminfo.allocated
        << ",g_meminfo.free " << g_meminfo.free
        << ",g_mem free rate " << 1.0 * g_meminfo.free / (g_meminfo.allocated + g_meminfo.free) * 100
        << ",alloc page cnt " << g_meminfo.page_alloc_cnt
        << ",free page cnt " << g_meminfo.page_free_cnt
        << ",tot slab " << tot_slab
        << endl;
}

void *alloc_one_page()
{
    if (!g_page_free_list)
    {
        return 0;
    }
    assert(g_meminfo.allocated < MEM_SIZE);
    assert(g_meminfo.free > 0);
    void *ret = g_page_free_list;
    g_page_free_list = *(void **)g_page_free_list;

#ifdef DDEBUG
    cout << __FILE__ << ':' << __LINE__ << ' ' << __FUNCTION__ << " allocated idx " << ((uint8 *)ret - g_mem) / PAGE_SIZE << endl;
    cout << __FILE__ << ':' << __LINE__ << ' ' << __FUNCTION__ << " next idx " << ((uint8 *)g_page_free_list - g_mem) / PAGE_SIZE << endl;
#endif
    assert(g_page_free_list != ret);

    g_meminfo.allocated += PAGE_SIZE;
    g_meminfo.free -= PAGE_SIZE;

    g_meminfo.page_alloc_cnt++;
    assert(g_meminfo.allocated <= MEM_SIZE);
    assert(g_meminfo.free >= 0);
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
#ifdef DDEBUG
    cout << __FILE__ << ':' << __LINE__ << ' ' << __FUNCTION__ << " freed idx " << ((uint8 *)addr - g_mem) / PAGE_SIZE << endl;
    cout << __FILE__ << ':' << __LINE__ << ' ' << __FUNCTION__ << " old header idx " << ((uint8 *)old_hd - g_mem) / PAGE_SIZE << endl;
#endif
    g_meminfo.allocated -= PAGE_SIZE;
    g_meminfo.free += PAGE_SIZE;
    g_meminfo.page_free_cnt++;
    assert(g_meminfo.free > 0);
    assert(g_meminfo.allocated < MEM_SIZE);
}

slab_t *alloc_one_slab(int obj_size, cache_t *parent_cache) // init
{
    slab_t *p_slab = (slab_t *)alloc_one_page();
    if (parent_cache && parent_cache->slab_list)
        assert(p_slab != parent_cache->slab_list);
    if (!p_slab)
        return 0;
    p_slab->prev = p_slab;
    p_slab->next = p_slab;
    p_slab->cache_ptr = parent_cache;
    p_slab->bitmap = (int *)(p_slab + 1);
    p_slab->obj_num = 0;
    p_slab->obj_num_limit = 0;
    int space = PAGE_SIZE - sizeof(slab_t);
    while (space > obj_size + sizeof(int))
    {
        space -= (obj_size + sizeof(int));
        p_slab->bitmap[p_slab->obj_num_limit++] = 0;
    }
    // set obj arr
    p_slab->obj_arr = (void *)&p_slab->bitmap[p_slab->obj_num_limit];
    assert(p_slab->obj_arr);
    // init obj list
    int i;
    for (i = 0; i + 1 < p_slab->obj_num_limit; i++)
    {
        *(void **)((uint8 *)p_slab->obj_arr + obj_size * i) = (void *)((uint8 *)p_slab->obj_arr + obj_size * (i + 1));
    }
    // last obj's next-addr set to null
    *(void **)((uint8 *)p_slab->obj_arr + obj_size * (p_slab->obj_num_limit - 1)) = 0;

    // set obj list
    p_slab->obj_list_hd = p_slab->obj_arr;

    if (p_slab->cache_ptr)
    {
#ifdef DDEBUG
        cout << __FILE__ << ':' << __LINE__ << ' ' << __FUNCTION__ << " befre alloc" << endl;
        if (parent_cache->slab_list)
            cout << "heaeder slab addr " << ((ull)parent_cache->slab_list - (ull)g_mem) / PAGE_SIZE << endl;
        else
            cout << "heaeder slab addr "
                 << "null" << endl;
        print_g_meminfo();
        print_cache(p_slab->cache_ptr);
#endif

        p_slab->cache_ptr->slab_num++;

        // link new slab to cache
        if (!parent_cache->slab_list)
        {
            parent_cache->slab_list = p_slab;
        }
        else
        {
            p_slab->prev = parent_cache->slab_list->prev;
            p_slab->next = parent_cache->slab_list;
            p_slab->prev->next = p_slab;
            p_slab->next->prev = p_slab;
        }
#ifdef DDEBUG
        cout << __FILE__ << ':' << __LINE__ << ' ' << __FUNCTION__ << " after alloc" << endl;
        cout << "heaeder slab addr " << ((ull)parent_cache->slab_list - (ull)g_mem) / PAGE_SIZE << ",new slab addr " << ((ull)p_slab - (ull)g_mem) / PAGE_SIZE << endl;
        print_g_meminfo();
        print_cache(p_slab->cache_ptr);
#endif
    }

    return p_slab;
}

void free_one_slab(slab_t *p_slab)
{
    assert(!p_slab->obj_num);
#ifdef DDEBUG
    cout << __FILE__ << ':' << __LINE__ << ' ' << __FUNCTION__ << " befre free ";
    print_g_meminfo();
    print_cache(p_slab->cache_ptr);
#endif
    if (p_slab->next != p_slab)
    {
        assert(p_slab->cache_ptr);
        assert(p_slab->cache_ptr->slab_num > 1);
        p_slab->next->prev = p_slab->prev;
        p_slab->prev->next = p_slab->next;
        // avoid slab_list is freed
        p_slab->cache_ptr->slab_list = p_slab->next;
        // clean freed slab
        p_slab->prev = p_slab->next = p_slab;
    }
    else
    {
        assert(p_slab->cache_ptr);
        assert(p_slab->cache_ptr->slab_num == 1);
        p_slab->cache_ptr->slab_list = 0;
    }

    p_slab->cache_ptr->slab_num--;
    free_one_page(p_slab);
#ifdef DDEBUG
    cout << __FILE__ << ':' << __LINE__ << ' ' << __FUNCTION__ << " after free ";
    print_g_meminfo();
    print_cache(p_slab->cache_ptr);
#endif
}

void *alloc_obj(int size) // 2^5~2^9
{                         // check size
    int i;
    cache_t *pcache = 0;
    slab_t *slab_ptr = 0;
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

    // find a slab to alloc new object
    // 1. alloc from existing slab
    if (pcache->slab_list)
    {
        slab_t *tmp = pcache->slab_list;
        do
        {
            if (tmp->obj_num < tmp->obj_num_limit)
            {
                alloc_from = 1;
                slab_ptr = tmp;
                break;
            }
            tmp = tmp->next;
        } while (tmp != pcache->slab_list);
    }

    // 2. all slabs are full || no slab, need to create new slab
    if (!slab_ptr)
    {
        alloc_from = 2;
        slab_ptr = alloc_one_slab(size, pcache);
    }

    // 3. handle corner case
    if (!slab_ptr)
    {
        alloc_from = 3;
        cout << "alloc_one_slab return null" << endl;
        return 0;
    }

    // check offset
    assert(slab_ptr->obj_list_hd >= slab_ptr->obj_arr);
    assert(((ull)slab_ptr->obj_list_hd - (ull)slab_ptr->obj_arr) % size == 0);
    // check bitmap
    ull idx = ((ull)slab_ptr->obj_list_hd - (ull)slab_ptr->obj_arr) / size;
    assert(!slab_ptr->bitmap[idx]);
    // modify slab's link list
    ret = slab_ptr->obj_list_hd;
    slab_ptr->obj_list_hd = *(void **)slab_ptr->obj_list_hd;
    // modify bitmap
    slab_ptr->bitmap[idx] = 1;
    // modify objnum
    slab_ptr->obj_num++;

    return ret;
}

void free_obj(void *addr)
{
    int i;
    for (i = 0; i < g_meminfo.end_order - g_meminfo.start_order; i++)
    {
        slab_t *slab_ptr = g_caches[i].slab_list;
        if (!slab_ptr)
            continue;
        do
        {
            void *st = slab_ptr->obj_arr;
            assert(st);
            void *ed = (uint8 *)slab_ptr->obj_arr + g_caches[i].obj_size * slab_ptr->obj_num_limit;
            if (addr < ed && addr >= st)
            {
                // check offset
                assert(((ull)addr - (ull)slab_ptr->obj_arr) % g_caches[i].obj_size == 0);
                // check bitmap
                ull idx = ((ull)addr - (ull)slab_ptr->obj_arr) / g_caches[i].obj_size;
                assert(slab_ptr->bitmap[idx]);
                // modify bitmap
                slab_ptr->bitmap[idx] = 0;
                // modify obj linklist
                *(void **)addr = slab_ptr->obj_list_hd;
                slab_ptr->obj_list_hd = addr;
                // modify objnum
                slab_ptr->obj_num--;
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

    int order;
    for (order = g_meminfo.start_order; order < g_meminfo.end_order; order++)
    {
        cache_t *cc = &g_caches[order - g_meminfo.start_order];
        cc->obj_size = 1 << order;
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
    *(void **)(g_mem + MEM_SIZE - PAGE_SIZE) = 0;
}

void test_page()
{
    void *ret;
    void **page_stack = (void **)alloc_one_page();
    int alloc_num = 0;
    while (1)
    {
        if (rand() & 1)
        {
            ret = alloc_one_page();
            if (!ret)
            {
                continue; // alloc_one_page fail
            }
            page_stack[alloc_num++] = ret;
        }
        else
        {
            if (alloc_num == 0)
                continue;
            free_one_page(page_stack[--alloc_num]);
        }
        if (rand() % 100000000 == 0)
        {
            print_g_meminfo();
        }
        assert(g_meminfo.allocated <= MEM_SIZE && g_meminfo.allocated >= 0);
        assert(g_meminfo.free <= MEM_SIZE && g_meminfo.free >= 0);
    }
}

void test_slab(int ord) // objsize = 1<<ord
{
    cache_t *cache_ptr = &g_caches[ord - g_meminfo.start_order];
    cache_ptr->slab_list = 0;
    cache_ptr->slab_num = 0;
    while (1)
    {
        if (rand() & 1)
        {
            alloc_one_slab(1 << 8, cache_ptr);
        }
        else
        {
            if (cache_ptr->slab_list)
            {
                free_one_slab(cache_ptr->slab_list);
            }
            else
            {
#ifdef DDEBUG
                cout << "cache_ptr->slab_list is null" << endl;
#endif
            }
        }
        if (rand() % 10000000 == 0)
        {
            print_g_meminfo();
        }

        assert(g_meminfo.allocated <= MEM_SIZE && g_meminfo.allocated >= 0);
        assert(g_meminfo.free <= MEM_SIZE && g_meminfo.free >= 0);
    }
}

void test_obj(int ord)
{
    void *ret = 0;
    void **obj_stack = (void **)alloc_one_page();
    int alloc_num = 0;
    while (1)
    {
        int op = rand() & 1;
        op_from = op;
        if (op)
        {
            if (alloc_num >= PAGE_SIZE / sizeof(void *))
                continue;
            ret = alloc_obj(1 << ord);
            obj_stack[alloc_num++] = ret;
        }
        else
        {
            if (alloc_num == 0)
                continue;
            free_obj(obj_stack[--alloc_num]);
        }

        if (rand() % 10000000 == 0)
        {
            print_g_meminfo();
        }
        assert((g_meminfo.page_alloc_cnt - g_meminfo.page_free_cnt) * PAGE_SIZE == g_meminfo.allocated);
        assert(g_meminfo.allocated + g_meminfo.free == MEM_SIZE);
    }
}

int main()
{
    srand(time(NULL));
    init_g_page_free_list();
    init_g_caches();
    // test_page();
    // test_slab(8);
    test_obj(9);

    return 0;
}