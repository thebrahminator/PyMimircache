

#include "heatmap.h"


void heatmap_nonLRU_hit_rate_start_time_end_time_thread(gpointer data, gpointer user_data){
    guint64 i, j, hit_count, miss_count;
    struct multithreading_params_heatmap* params = (struct multithreading_params_heatmap*) user_data;
    READER* reader_thread = copy_reader(params->reader);
    GArray* break_points = params->break_points;
    guint64* progress = params->progress;
    draw_dict* dd = params->dd;
    struct cache* cache = params->cache->core->cache_init(params->cache->core->size, params->cache->core->data_type,
                                                          params->cache->core->cache_init_params);
    
    int order = GPOINTER_TO_INT(data)-1;
    
    hit_count = 0;
    miss_count = 0;


    // create cache lize struct and initialization
    cache_line* cp = (cache_line*)malloc(sizeof(cache_line));
    cp->op = -1;
    cp->size = -1;
    cp->valid = TRUE;
    
    skip_N_elements(reader_thread, g_array_index(break_points, guint64, order));
    
    // this is for synchronizing ts in cache, which is used as index for access next_access array 
    if (cache->core->type == e_Optimal)
        ((struct optimal_params*)(cache->cache_params))->ts = g_array_index(break_points, guint64, order);
    
    for (i=order; i<break_points->len-1; i++){
        for(j=0; j< g_array_index(break_points, guint64, i+1) - g_array_index(break_points, guint64, i); j++){
            read_one_element(reader_thread, cp);
            if (cache->core->add_element(cache, cp))
                hit_count++;
            else
                miss_count++;
        }
        dd->matrix[order][i] = (double)(hit_count)/(hit_count+miss_count);
    }

    for(j=0; j< reader_thread->total_num - g_array_index(break_points, guint64, break_points->len - 1); j++){
        read_one_element(reader_thread, cp);
        if (!cp->valid)
            printf("detect error in heatmap_nonLRU_hit_rate_start_time_end_time, difference: %llu\n",
                   reader_thread->total_num - g_array_index(break_points, guint64, break_points->len - 1) - j);
        if (cache->core->add_element(cache, cp))
            hit_count++;
        else
            miss_count++;
    }
    dd->matrix[order][i] = (double)(hit_count)/(hit_count+miss_count);
    

    // clean up
    g_mutex_lock(&(params->mtx));
    (*progress) ++ ;
    g_mutex_unlock(&(params->mtx));
    free(cp);
    if (reader_thread->type != 'v')
        close_reader(reader_thread);
    else
        free(reader_thread);
    cache->core->destroy_unique(cache);
}


void heatmap_LRU_hit_rate_start_time_end_time_thread(gpointer data, gpointer user_data){

    guint64 i, j, hit_count, miss_count;
    struct multithreading_params_heatmap* params = (struct multithreading_params_heatmap*) user_data;
    READER* reader_thread = copy_reader(params->reader);
    GArray* break_points = params->break_points;
    guint64* progress = params->progress;
    draw_dict* dd = params->dd;
    guint64 cache_size = (guint64)params->cache->core->size;
    gint* last_access = reader_thread->last_access;
    long long* reuse_dist = reader_thread->reuse_dist;
    
    int order = GPOINTER_TO_INT(data)-1;
    guint64 real_start = g_array_index(break_points, guint64, order);
    
    hit_count = 0;
    miss_count = 0;
    

    skip_N_elements(reader_thread, g_array_index(break_points, guint64, order));

    for (i=order; i<break_points->len-1; i++){
        
        for(j=g_array_index(break_points, guint64, i); j< g_array_index(break_points, guint64, i+1); j++){
            if (reuse_dist[j] == -1)
                miss_count ++;
            else if (last_access[j] - (long long)(j - real_start) <= 0 && reuse_dist[j] < (long long)cache_size)
                hit_count ++;
            else
                miss_count ++;
//            printf("last access %d, j %lu, real_start %lu, reuse dist %lld, cache_size %lu, hitcount: %lu\n", last_access[j], j, real_start, reuse_dist[j], cache_size, hit_count);
        }
        dd->matrix[order][i] = (double)(hit_count)/(hit_count+miss_count);
    }
    
    for(j=g_array_index(break_points, guint64, break_points->len - 1); j<(guint64)reader_thread->total_num; j++){
        if (reuse_dist[j] == -1)
            miss_count ++;
        else if (last_access[j] - (j - real_start) <=0 && (guint64)reuse_dist[j] < cache_size)
            hit_count ++;
        else
            miss_count ++;
    }
    dd->matrix[order][i] = (double)(hit_count)/(hit_count+miss_count);
//    printf("one around, hit count: %lu, miss count: %lu, matrix hr: %lf\n", hit_count, miss_count, dd->matrix[order][i]);
    
    // clean up
    g_mutex_lock(&(params->mtx));
    (*progress) ++ ;
    g_mutex_unlock(&(params->mtx));
    if (reader_thread->type != 'v')
        close_reader(reader_thread);
    else
        free(reader_thread);
}


void heatmap_rd_distribution_thread(gpointer data, gpointer user_data){
    
    guint64 j;
    struct multithreading_params_heatmap* params = (struct multithreading_params_heatmap*) user_data;
    GArray* break_points = params->break_points;
    guint64* progress = params->progress;
    draw_dict* dd = params->dd;
    long long* reuse_dist = params->reader->reuse_dist;
    double log_base = params->log_base;
    
    guint64 order = (guint64)GPOINTER_TO_INT(data)-1;
    double* array = dd->matrix[order];
    
    
    if (order != break_points->len-1){
        for(j=g_array_index(break_points, guint64, order); j< g_array_index(break_points, guint64, order+1); j++){
            if (reuse_dist[j] == 0 ||reuse_dist[j] == 1)
                array[0] += 1;
            else
                array[(long)(log(reuse_dist[j])/(log(log_base)))] += 1;
        }
    }
    else{
        for(j=g_array_index(break_points, guint64, order); (long long)j< params->reader->total_num; j++)
            if (reuse_dist[j] == 0 ||reuse_dist[j] == 1)
                array[0] += 1;
            else
                array[(long)(log(reuse_dist[j])/(log(log_base)))] += 1;
    }

    
    // clean up
    g_mutex_lock(&(params->mtx));
    (*progress) ++ ;
    g_mutex_unlock(&(params->mtx));
}
