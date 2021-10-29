#!/bin/sh
#liveness analyze
runNetwork() {
	echo	"runNetwork	: $1"
	echo	"log file	: $2"
	echo	"batchsize	: $3"
	echo	"./testing/$1 $3 > $2"
	`./testing/$1 $3 > ../result/$2`
	ret_val=$?
	return $ret_val
}


#define LIVENESS
#define RECOMPUTE_ON

testFunc() {
	echo
	echo	"-----------------------------------"
	echo	"network 		: $1"
	echo	"LIVENESS		: $2"
	echo	"RECOMPUTE_ON	: $3"
	echo	"batchsize		: $4"
	
	cmd="cmake "
	if [ "$2" = true ]; then
		cmd="${cmd} -DLIVENESS=1"
	fi
	if [ "$3" = true ]; then
		cmd="${cmd} -DRECOMPUTE_ON=1"
	fi

	rm -rf /content/superneurons-release/build/*
	cmd="${cmd} .."
	echo $cmd
	`$cmd`
	make release -j

	runNetwork $1 "$1_L:$2_R:$3_$4.log" $4
	ret_val=$?

	echo
	echo	"-----------------------------------" 
	return $ret_val
}

cifar10() {
	batch=128
	ret_val=0
	while : ; do
		testFunc cifar10 $1 $2 $batch
		if [ $? -eq 0 ]; then
			break
		fi
		batch=`expr $batch + 128`
	done
}

#define LIVENESS
#define RECOMPUTE_ON
#define LARGER
#define LRU_ON
#define BLASX_MALLOC
cifar10 false false #baseline
cifar10 true false #liveness 
cifar10 true true #liveness+recompute