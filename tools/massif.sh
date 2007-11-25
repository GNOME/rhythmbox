#!/bin/sh
G_DEBUG=gc-friendly G_SLICE=always-malloc valgrind --trace-children=yes --tool=massif --depth=15  --alloc-fn=g_malloc --alloc-fn=g_realloc --alloc-fn=g_try_malloc --alloc-fn=g_malloc0 --alloc-fn=g_mem_chunk_alloc --alloc-fn=g_mem_chunk_alloc0 --alloc-fn=g_try_malloc0 --alloc-fn=g_try_realloc --alloc-fn=ft_alloc --alloc-fn=g_slice_alloc --alloc-fn=g_slice_alloc0 shell/rhythmbox $*
