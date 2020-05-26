#!/bin/bash
#SBATCH --job-name="stream-xeon-gold-6130-single-core-indirect-dot-product"
#SBATCH --partition=xeongold16q
#SBATCH --time=0-01:00:00
#SBATCH --nodes=1
#SBATCH --ntasks-per-node=1
#SBATCH --cpus-per-task=1
#SBATCH --exclusive
#SBATCH --output=jobs/%j-%x.out
#SBATCH --error=jobs/%j-%x.err

set -o errexit

##
## This script is used to run a series of STREAM benchmarks on a
## single core of an Intel Xeon Gold 6130 multi-core CPU on eX3, an
## Experimental Infrastructure for Exploration of Exascale Computing.
##
## Suggested configuration:
##   1. Set STREAM_DIR to point to the STREAM source directory
##   2. Set TMPDIR=/work/${USER}/tmp
##   3. Set CC=gcc
##   4. Set CFLAGS="-march=skylake-avx512 -O3 -funroll-loops -fno-builtin"
##
function main ()
{
    local job_id="${SLURM_JOB_ID:-0}"
    local job_name="${SLURM_JOB_NAME:-0}"
    local job_host="${SLURM_SUBMIT_HOST}"
    local job_nodelist="${SLURM_JOB_NODELIST}"

    echo "SLURM_JOB_NAME=${SLURM_JOB_NAME}" >&2
    echo "SLURM_JOB_ID=${SLURM_JOB_ID}" >&2
    echo "SLURM_SUBMIT_HOST=${SLURM_SUBMIT_HOST}" >&2
    echo "SLURM_JOB_NODELIST=${SLURM_JOB_NODELIST}" >&2

    # Use the environment variable STREAM_DIR to find the location of
    # the STREAM benchmark.
    if [[ ! -d "${STREAM_DIR}" ]]; then
	printf "Please set STREAM_DIR to specify the location of the source code for the STREAM benchmark.\n" >&2
	exit 1
    fi

    # Create a temporary working directory
    workdir="$(mktemp --tmpdir -d ${job_id}-${job_name}-XXXX)"

    # Set up the compiler flags for each benchmark run
    local configurations=(
	"-DSTREAM_ARRAY_SIZE=$((1*1024)) -DNTIMES=10 -DUSE_CLOCK_GETTIME -DENABLE_INDIRECT_DOT_PRODUCT"
	"-DSTREAM_ARRAY_SIZE=$((2*1024)) -DNTIMES=10 -DUSE_CLOCK_GETTIME -DENABLE_INDIRECT_DOT_PRODUCT"
	"-DSTREAM_ARRAY_SIZE=$((4*1024)) -DNTIMES=10 -DUSE_CLOCK_GETTIME -DENABLE_INDIRECT_DOT_PRODUCT"
	"-DSTREAM_ARRAY_SIZE=$((8*1024)) -DNTIMES=10 -DUSE_CLOCK_GETTIME -DENABLE_INDIRECT_DOT_PRODUCT"
	"-DSTREAM_ARRAY_SIZE=$((16*1024)) -DNTIMES=10 -DUSE_CLOCK_GETTIME -DENABLE_INDIRECT_DOT_PRODUCT"
	"-DSTREAM_ARRAY_SIZE=$((32*1024)) -DNTIMES=10 -DUSE_CLOCK_GETTIME -DENABLE_INDIRECT_DOT_PRODUCT"
	"-DSTREAM_ARRAY_SIZE=$((64*1024)) -DNTIMES=10 -DUSE_CLOCK_GETTIME -DENABLE_INDIRECT_DOT_PRODUCT"
	"-DSTREAM_ARRAY_SIZE=$((128*1024)) -DNTIMES=10 -DUSE_CLOCK_GETTIME -DENABLE_INDIRECT_DOT_PRODUCT"
	"-DSTREAM_ARRAY_SIZE=$((256*1024)) -DNTIMES=10 -DUSE_CLOCK_GETTIME -DENABLE_INDIRECT_DOT_PRODUCT"
	"-DSTREAM_ARRAY_SIZE=$((512*1024)) -DNTIMES=10 -DUSE_CLOCK_GETTIME -DENABLE_INDIRECT_DOT_PRODUCT"
	"-DSTREAM_ARRAY_SIZE=$((1*1024*1024)) -DNTIMES=10 -DUSE_CLOCK_GETTIME -DENABLE_INDIRECT_DOT_PRODUCT"
	"-DSTREAM_ARRAY_SIZE=$((2*1024*1024)) -DNTIMES=10 -DUSE_CLOCK_GETTIME -DENABLE_INDIRECT_DOT_PRODUCT"
	"-DSTREAM_ARRAY_SIZE=$((4*1024*1024)) -DNTIMES=10 -DUSE_CLOCK_GETTIME -DENABLE_INDIRECT_DOT_PRODUCT"
	"-DSTREAM_ARRAY_SIZE=$((8*1024*1024)) -DNTIMES=10 -DUSE_CLOCK_GETTIME -DENABLE_INDIRECT_DOT_PRODUCT"
	"-DSTREAM_ARRAY_SIZE=$((16*1024*1024)) -DNTIMES=10 -DUSE_CLOCK_GETTIME -DENABLE_INDIRECT_DOT_PRODUCT"
	"-DSTREAM_ARRAY_SIZE=$((32*1024*1024)) -DNTIMES=10 -DUSE_CLOCK_GETTIME -DENABLE_INDIRECT_DOT_PRODUCT"
	"-DSTREAM_ARRAY_SIZE=$((64*1024*1024)) -DNTIMES=10 -DUSE_CLOCK_GETTIME -DENABLE_INDIRECT_DOT_PRODUCT")

    # Compile and run the benchmark
    for i in "${!configurations[@]}"; do
	local STREAM_CFLAGS="${configurations[${i}]}"
	local stream="${workdir}/stream-$(printf '%03d' ${i})"
	set -x
	${CC} ${CFLAGS} ${STREAM_CFLAGS} "${STREAM_DIR}/stream.c" -o "${stream}"
	numactl --physcpubind=0 --membind=0 ${stream}
	{ set +x; } 2>/dev/null
    done
}

main "$@"
