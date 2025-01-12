//
// Created by ay27 on 9/16/17.
//

#include <recompute.h>

namespace SuperNeurons {



///////////////////////////////////////////////////////////////////////////////////////
// init
template <class value_type>
void recompute_t<value_type>::find_bottleneck() {
    bottleneck_mem_size = 0;
    f_mem_usage.clear();
    b_mem_usage.clear();
    f_mem_usage.resize(max_layer_id + 1);
    b_mem_usage.resize(max_layer_id + 1);

    int bottleneck_layer_id = -1;
    for (int curt_layer_id = 1; curt_layer_id <= max_layer_id; ++curt_layer_id) {

        auto tmp = reg->get_net_layers().find(curt_layer_id);
        if (tmp == reg->get_net_layers().end()) {
            continue;
        }
        base_layer_t<value_type> *curt_l = (base_layer_t<value_type> *) tmp->second;

        if (curt_l->get_layer_type() == DATA_L) {
            continue;
        }

        /*--------------------------------------------------------------------------*/
        // calculate forward dependency memory usage
        size_t tmp1 = 0;
        auto ftensors = reg->get_forward_dependency(curt_layer_id); //当前层的依赖tensors
        for (auto t = ftensors->begin(); t != ftensors->end(); ++t) {
            if ((*t)->get_type() != DATA && (*t)->get_type() != CONV_BUFF) {//只考虑这两种类型
                continue;
            }

            if (curt_l->get_next().empty()) {
                continue;
            }

            // don't accumulate output!!!输入输出重复的不累计
            for (size_t i = 0; i < curt_l->get_next().size(); ++i) {
                if ((*t) == reg->get_reg_output(curt_layer_id, curt_l->get_next()[i]->get_base_id())) {
                    continue;
                }
            }

            tmp1 += (*t)->get_mem_size();
        }
        f_mem_usage[curt_layer_id] = tmp1;
        /*--------------------------------------------------------------------------*/


        /*--------------------------------------------------------------------------*/
        size_t tmp2 = 0;
        auto btensors = reg->get_backward_dependency(curt_layer_id);
        for (auto t = btensors->begin(); t != btensors->end(); ++t) {
            if ((*t)->get_type() != DATA && (*t)->get_type() != CONV_BUFF) {
                continue;
            }

            tmp2 += (*t)->get_mem_size();
        }

        // log the memory usage in this layer
        b_mem_usage[curt_layer_id] = tmp2;

        // backward bottleneck
        if (tmp2 > bottleneck_mem_size) {   //反向传播的瓶颈才是瓶颈
            bottleneck_mem_size = tmp2;
            bottleneck_layer_id = curt_layer_id;
        }
        /*--------------------------------------------------------------------------*/

#ifdef MEM_DEBUG
        printf("layer: %d, fmem: %zu, bmem: %zu\n", curt_layer_it, tmp1, tmp2);
#endif
    }

#ifdef DEBUG
    printf("-------------bottleneck-------------\n");
    printf("layer %d: bottleneck mem size : %f MB\n", bottleneck_layer_id,
            (double) bottleneck_mem_size / 1024.0 / 1024.0);
    for (int layer_id = 1; layer_id <= max_layer_id; ++layer_id) {
        printf("layer %d: fmem: %f MB, bmem: %f MB\n",
               layer_id, (double)f_mem_usage[layer_id] / 1024.0 / 1024.0, (double)b_mem_usage[layer_id]/1024.0/1024.0);
    }

#endif
}

template <class value_type>
void recompute_t<value_type>::scan_checkpoint() {
    // we don't care the structure layer such as fork, join, concat, etc.
    // so, in searching checkpoint, it's in linear.
    checkpoints.clear();
    seg_mems.clear();
    checkpoints.resize(max_layer_id + 1);
    seg_mems.resize(max_layer_id + 1);

    for (auto it = reg->get_net_comp_route().begin(); it != reg->get_net_comp_route().end(); ++it) {
        if (it->second == BACKWARD) {
            continue;
        }

        int curt_layer_id = it->first;

        auto tmp = reg->get_net_layers().find(curt_layer_id);
        if (tmp == reg->get_net_layers().end()) {
            continue;
        }
        base_layer_t<value_type> *curt_l = (base_layer_t<value_type> *) tmp->second;
        if (curt_l->get_layer_type() == DATA_L) {   //非DATA_L才需要scan ckp
            continue;
        }

        if (!is_checkpoint(curt_l)) {   //非CHECKPOINT_LAYERS才需要scan ckp
            continue;
        }

        for (size_t i = 0; i < curt_l->get_next().size(); ++i) {
            base_layer_t<value_type>* curt_checkpoint=curt_l->get_next()[i];
            size_t curt_seg_mem = 0;    //段内存
            bool larger_than_bottleneck = false;
            while (!is_checkpoint(curt_checkpoint)) {   //两个CHECKPOINT_LAYERS间为一段,从curt到下个ckp的段
                curt_seg_mem += f_mem_usage[curt_checkpoint->get_base_id()];
                if (curt_seg_mem > bottleneck_mem_size) {
                    larger_than_bottleneck = true;
                }
                checkpoints[curt_checkpoint->get_base_id()] = std::make_pair(curt_l, i);    //ckp[curt.next[i]]=(curt,i)
                                                                                            //ckp[curt.next[i].next[0]]=(curt,i)
                                                                                            //...
                                                                                            //ckp[xx]=(curt,i),xx.next[0]为ckp
                // inner checkpoint, it's linear
                curt_checkpoint = curt_checkpoint->get_next()[0];
            }
            if (checkpoints[curt_checkpoint->get_base_id()].first == NULL) {    //ckp[ckp].first == null
                checkpoints[curt_checkpoint->get_base_id()] = std::make_pair(curt_l, i);    //ckp[ckp]=(curt,i)
            }

            curt_checkpoint=curt_l->get_next()[i];
            curt_seg_mem = 0;
            while (!is_checkpoint(curt_checkpoint)) {
                curt_seg_mem += f_mem_usage[curt_checkpoint->get_base_id()];
                seg_mems[curt_checkpoint->get_base_id()] = std::make_pair(curt_seg_mem, larger_than_bottleneck);//sm[curt.next[i]]=(,false)
                                                                                                                //sm[curt.next[i].next[0]]=(,false)
                                                                                                                //...
                                                                                                                //sm[xx]=(,true),xx.next[0]为ckp

                // inner checkpoint, it's linear
                curt_checkpoint = curt_checkpoint->get_next()[0];
            }
        }

    }   //遍历计算图

    for (int curt_layer_id = max_layer_id; curt_layer_id > 0; --curt_layer_id) {

        if (checkpoints[curt_layer_id].first != NULL && checkpoints[curt_layer_id].second == 0) {
            continue;
        }

        auto tmp = reg->get_net_layers().find(curt_layer_id);
        if (tmp == reg->get_net_layers().end()) {
            continue;
        }
        base_layer_t<value_type> *curt_l = (base_layer_t<value_type> *) tmp->second;
        if (curt_l->get_layer_type() == DATA_L) {
            continue;
        }
        if (is_checkpoint(curt_l)) {
            continue;
        }

        base_layer_t<value_type> *pre_checkpoint = curt_l;
        base_layer_t<value_type> *pre_next_checkpoint = pre_checkpoint;
        while (pre_checkpoint != NULL && !is_checkpoint(pre_checkpoint)) {
            pre_next_checkpoint = pre_checkpoint;
            if (pre_checkpoint->get_prev().empty()) {
                break;
            }
            pre_checkpoint = pre_checkpoint->get_prev()[0];
        }
        if (pre_checkpoint == NULL || !is_checkpoint(pre_checkpoint)) {
            // this layer has no previous checkpoint
            continue;
        }

        size_t branch_idx = 0;  //分支要么0要么1
        for (size_t i = 0; i < pre_checkpoint->get_next().size(); ++i) {
            if (pre_checkpoint->get_next()[i] == pre_next_checkpoint) {
                branch_idx = i;
                break;
            }
        }

        size_t mem = 0;
        bool larger_than_bottleneck = false;
        base_layer_t<value_type> *it = pre_next_checkpoint;
        while (true) {
            int it_id = it->get_base_id();
            mem += f_mem_usage[it_id];
            if (mem > bottleneck_mem_size) {
                larger_than_bottleneck = true;
                break;
            }
            if (it == curt_l) {
                break;
            }
            it = it->get_next()[0];
        }

        mem = 0;
        it = pre_next_checkpoint;
        while (true) {
            int it_id = it->get_base_id();
            mem += f_mem_usage[it_id];
            seg_mems[it_id] = std::make_pair(mem, larger_than_bottleneck);
            checkpoints[it_id] = std::make_pair(pre_checkpoint, branch_idx);
            if (it == curt_l) {
                break;
            }
            it = it->get_next()[0];
        }
    }

#ifdef DEBUG
    printf("--------checkpoints----------\n");
    for (int layer_id = 1; layer_id <= max_layer_id; ++layer_id) {
        printf("layer %d: checkpoint layer: %d , branch idx: %d\n", layer_id,   //扫描出每个层对应的ckp层
               checkpoints[layer_id].first == NULL ? 0
                                                   : ((base_layer_t<value_type> *) (checkpoints[layer_id].first))->get_base_id(),
               checkpoints[layer_id].second);
    }
    printf("--------seg_mems---------\n");
    for (int layer_id = 1; layer_id <= max_layer_id; ++layer_id) {
        printf("layer %d: mem: %f MB, larger: %d\n",    //扫描出每个层，其段的内存量
               layer_id, (double) seg_mems[layer_id].first / 1024.0 / 1024.0, seg_mems[layer_id].second);
    }
#endif
}

template <class value_type>
void recompute_t<value_type>::scan_offload_tensors() {
    offload_tensors.clear();
    offload_tensors.resize(max_layer_id + 1);

    for (int curt_layer_id = 1; curt_layer_id <= max_layer_id; ++curt_layer_id) {
        offload_tensors[curt_layer_id].resize(0);

        auto tmp = reg->get_net_layers().find(curt_layer_id);
        if (tmp == reg->get_net_layers().end()) {
            continue;
        }
        base_layer_t<value_type> *curt_layer = (base_layer_t<value_type> *) tmp->second;
        if (curt_layer->get_layer_type() == DATA_L) {
            continue;
        }
        if (!is_checkpoint(curt_layer)) {   //  curt不是ckp
            tensor_t<value_type>* in = reg->get_reg_output(curt_layer->get_prev()[0]->get_base_id(), curt_layer_id);    //  pre-curt的output
            if (!is_checkpoint(curt_layer->get_prev()[0])) {    //  pre不是ckp，则将curt的输入加入offload list
                offload_tensors[curt_layer_id].push_back((void *) in);
            }
        } else {    //curt是ckp
            for (size_t i = 0; i < curt_layer->get_prev().size(); ++i) {
                base_layer_t<value_type> *prev_layer = curt_layer->get_prev()[i];

                if (prev_layer == NULL) continue;
                if (prev_layer->get_layer_type() == DATA_L) {//为啥？数据层无运算
                    continue;
                }

                // we don't scan checkpoint, the output of checkpoint layer will not be freed.
                if (is_checkpoint(prev_layer)) {
                    continue;
                }

                int prev_layer_id = prev_layer->get_base_id();
                int curt_layer_id = curt_layer->get_base_id();

                tensor_t<value_type> *t = reg->get_reg_output(prev_layer_id, curt_layer_id);

                if (t == NULL) continue;
                assert(t != NULL);
                assert(t->get_type() == DATA);

                offload_tensors[curt_layer_id].push_back((void *) t);   //  pre不是ckp，且...则将curt的输入加入offload list
            }
        }
    }
#ifdef DEBUG
    printf("------offload tensors-----------\n");   //添加本层的依赖tensor
    for (int layer_id = 1; layer_id <= max_layer_id; ++layer_id) {
        printf("layer %d: \n", layer_id);
        if (offload_tensors[layer_id].empty()) {
            continue;
        }
        for (size_t i = 0; i < offload_tensors[layer_id].size(); ++i) {
            tensor_t<value_type> *t = (tensor_t<value_type> *) offload_tensors[layer_id][i];
            printf("tensor %p type %d, its layer is %d\n", t, t->get_type(), t->get_layer_id());
        }
    }
#endif
}


template <class value_type>
void recompute_t<value_type>::scan_recompute_free_tensor() {
    recompute_free_map.clear();
    recompute_free_map.resize(max_layer_id+1);

    for (int curt_layer_id = 1; curt_layer_id <= max_layer_id; ++curt_layer_id) {
        recompute_free_map[curt_layer_id].resize(0);

        auto tmp = reg->get_net_layers().find(curt_layer_id);
        if (tmp == reg->get_net_layers().end()) {
            continue;
        }
        base_layer_t<value_type>* curt_l = (base_layer_t<value_type>*)tmp->second;

        if (is_checkpoint(curt_l)) {    //当前层为ckp层的话，则当前层不进行重计算
            continue;
        }
        for (size_t i = 0; i < reg->get_forward_dependency(curt_layer_id)->size(); ++i) {
            tensor_t<value_type>* t= reg->get_forward_dependency(curt_layer_id)->operator[](i); //当前层的依赖tensor t
            if (t->get_type() != DATA && t->get_type() != CONV_BUFF) {  //依赖tensor的类型不是DATA/CONV_BUFF时，本层不进行重计算
                continue;
            }
            // don't free output    本层的依赖tensor == 本层的输出tensor时，本层不进行重计算
            if (t == reg->get_reg_output(curt_layer_id, curt_l->get_next()[0]->get_base_id())) {
                continue;
            }
            // don't free input from checkpoint 本层的输入tensor来自ckp层时，本层不进行重计算
            if (t == reg->get_reg_output(curt_l->get_prev()[0]->get_base_id(), curt_layer_id) && is_checkpoint(curt_l->get_prev()[0])) {
                continue;
            }
            recompute_free_map[curt_layer_id].push_back((void*)t);//添加本层的依赖tensor
        }
    }
#ifdef DEBUG
    printf("--------recompute free tensor------------\n");  
    for (int layer_id = 1; layer_id <= max_layer_id; ++layer_id) {
        printf("layer %d:\n", layer_id);
        if (recompute_free_map[layer_id].empty()){
            continue;
        }
        for (size_t i = 0; i < recompute_free_map[layer_id].size(); ++i) {
            tensor_t<value_type>* t = (tensor_t<value_type>*)recompute_free_map[layer_id][i];
            printf("tensor %p, layer %d\n", t, t->get_layer_id());
        }
    }
#endif
}

///////////////////////////////////////////////////////////////////////////////////////

template <class value_type>
void recompute_t<value_type>::offload_to_recompute(int layer_id, net_comp dir, network_stage stage) {
    //let's kick out the structure layer here
    if (dir == BACKWARD) return;

    if (!is_re_init_finish && forward_flags[layer_id]) {
        re_init();
    }
    forward_flags[layer_id] = true;

    for (size_t i = 0; i < offload_tensors[layer_id].size(); ++i) {
        tensor_t<value_type>* t = (tensor_t<value_type>*)offload_tensors[layer_id][i];
        if (stage == NET_TRAIN) {
            // set it RECOMPUTE, avoid data transfer
            t->free_gpu_space(RECOMPUTE);   //将offload_tensors中的tensor释放并标记为重算
        } else {
            t->free_gpu_space(VOID);
        }
#ifdef LRU_ON
        lru->remove_item(lru->find(t));
#endif

#ifdef MEM_DEBUG
        printf("tensor:%p will be recomputed as curt layer is:(%d, %d)\n", t, curt_layer_id, curt_layer_type );
#endif
    }
}

template <class value_type>
void recompute_t<value_type>::stash_f_tensor(base_layer_t<value_type> *l) {
    auto vec = reg->get_forward_dependency(l->get_base_id());
    for (size_t i = 0; i < vec->size(); ++i) {
        tensor_t<value_type>* t = vec->operator[](i);
        if (t->get_type() != CONV_BUFF && t->get_type() != DATA) {
            continue;
        }

        if (t->get_type() == CONV_BUFF) {
            t->stash_gpu_space();
        } else {
            t->CPUtoGPU();//data要回到gpu，copy
        }

    }
}

template <class value_type>
void recompute_t<value_type>::update_f_tensor(base_layer_t<value_type> *l, int curt_recomputed_layer_id) {
    int curt_layer_id = l->get_base_id();

    // if curt segment does not exceed bottleneck, then skip
    if (! seg_mems[curt_layer_id].second) {//段长小，就待在gpu中，不free了
        return;
    }

#ifndef LARGER
    return;
#endif

    for (size_t i = 0; i < recompute_free_map[curt_layer_id].size(); ++i) { //为啥要用这个数组？
        tensor_t<value_type>* t = (tensor_t<value_type>*)recompute_free_map[curt_layer_id][i];

        if (is_backward_dependency(t, curt_recomputed_layer_id)) {
            continue;
        }

        if (is_in_offload_list(t)) {//为啥俩数组都用了？
            t->free_gpu_space(RECOMPUTE);//释放显存
        } else {
            t->free_gpu_space(VOID);
        }
    }
}

template <class value_type>
void recompute_t<value_type>::recompute_for_dependency(int curt_layer_id, network_stage stage) {

    base_layer_t<value_type>* pre_checkpoint = (base_layer_t<value_type>*)checkpoints[curt_layer_id].first;
    int branch = checkpoints[curt_layer_id].second;

#ifdef DEBUG
    printf("recompute layer from %d to %d\n", pre_checkpoint->get_base_id(), curt_layer_id);
#endif

    base_layer_t<value_type>* start_l = pre_checkpoint->get_next()[branch];

    while (start_l->get_base_id() != curt_layer_id) {

        tensor_t<value_type>* output_tensor = reg->get_reg_output(start_l->get_base_id(), start_l->get_next()[0]->get_base_id());
        if (output_tensor->get_state() == GPU_FUL && is_in_offload_list(output_tensor)) {
            // we do nothing this layer, as the output already in gpu
        } else {

            stash_f_tensor(start_l);
#ifdef DEBUG
            printf("forward layer %d, type %d\n", start_l->get_base_id(), start_l->get_layer_type());
#endif
            start_l->forward(stage, &cublas_handle, &cudnn_handle, reg);
            update_f_tensor(start_l, curt_layer_id);
        }

        // inner checkpoint, the net will be linear!
        start_l = start_l->get_next()[0];
    }

    if (is_checkpoint(start_l)) {
        return;
    }

    tensor_t<value_type>* output_tensor = reg->get_reg_output(start_l->get_base_id(), start_l->get_next()[0]->get_base_id());
    if (output_tensor->get_state() == GPU_FUL && is_in_offload_list(output_tensor)) {
        // we do nothing this layer, as the output already in gpu
        return;
    }
    stash_f_tensor(start_l);
#ifdef DEBUG
    printf("forward current layer %d, type %d\n", start_l->get_base_id(), start_l->get_layer_type());
#endif
    start_l->forward(stage, &cublas_handle, &cudnn_handle, reg);
    update_f_tensor(start_l, curt_layer_id);
}


template <class value_type>
void recompute_t<value_type>::upload_to_reconstruct(int layer_id, net_comp dir, network_stage stage) {
    if (dir == FORWARD) return;

    if (should_recompute[layer_id]) {
        recompute_for_dependency(layer_id, stage);
    }
}

template <class value_type>
inline void recompute_t<value_type>::re_init() {
    if (is_re_init_finish) {
        return;;
    }

#ifdef DEBUG
    printf("\n\n---------------------------\n");
    printf("recompute re-init\n");
#endif

    this->init();

    is_re_init_finish = true;
}

INSTANTIATE_CLASS(recompute_t);

} // namespace SuperNeurons