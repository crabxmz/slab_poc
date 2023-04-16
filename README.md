# slab_poc
self crafted slab allocator based on 4096 page size

# build and run
g++ -g slab.cpp   && ./a.out

# fuzz test function
test_page,test_slab,test_obj

# todo
add color for cacheline
make slab to free_list,partial_list,full_list
