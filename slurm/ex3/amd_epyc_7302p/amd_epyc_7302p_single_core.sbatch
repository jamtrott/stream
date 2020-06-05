#!/bin/bash
#SBATCH --job-name="stream_epyc_7302p_single_core"
#SBATCH --partition=rome16q
#SBATCH --time=0-01:00:00
#SBATCH --nodes=1
#SBATCH --nodelist=n049
#SBATCH --ntasks-per-node=1
#SBATCH --cpus-per-task=1
#SBATCH --exclusive
#SBATCH --output=jobs/%j-%x.out
#SBATCH --error=jobs/%j-%x.err

set -o errexit

##
## This script is used to run a series of STREAM benchmarks on a
## single core of an AMD Epyc 7302P multi-core CPU on eX3, an
## Experimental Infrastructure for Exploration of Exascale Computing.
##
## Suggested configuration:
##   1. Set STREAM_DIR to point to the STREAM source directory
##   2. Set TMPDIR=/work/${USER}/tmp
##   3. Set CC=gcc
##   4. Set CFLAGS="-march=znver2 -O3"
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
    echo "STREAM_DIR=${STREAM_DIR}" >&2
    echo "TMPDIR=${TMPDIR}" >&2
    echo "CC=${CC}" >&2
    echo "CFLAGS=${CFLAGS}" >&2
    uname -a >&2
    lscpu >&2
    numactl -H >&2

    # Use the environment variable STREAM_DIR to find the location of
    # the STREAM benchmark.
    if [[ ! -d "${STREAM_DIR}" ]]; then
	printf "Please set STREAM_DIR to specify the location of the source code for the STREAM benchmark.\n" >&2
	exit 1
    fi

    # Use the environment variable CC to invoke the compiler
    if [[ -z "${CC}" ]]; then
	printf "Please set CC to specify which C compiler to use.\n" >&2
	exit 1
    fi

    # Create a temporary working directory
    workdir="$(mktemp --tmpdir -d ${job_id}-${job_name}-XXXX)"

    # Set up the compiler flags for each benchmark run
    local stream_compiler_flags=(
	"-DSTREAM_ARRAY_SIZE=80000000ull -DNTIMES=10 -DUSE_CLOCK_GETTIME")

    local stream_environment_variables=(
	"OMP_NUM_THREADS=1 OMP_PROC_BIND=close OMP_PLACES=cores")

    local stream_numactl_options=(
	"--physcpubind=0 --membind=0")

    # Compile and run the benchmark
    for i in "${!stream_compiler_flags[@]}"; do
	local STREAM_CFLAGS="${stream_compiler_flags[${i}]}"
	local numactl_options="${numactl_options[${i}]}"
	local stream="${workdir}/stream-$(printf '%03d' ${i})"
	set -x
	${CC} ${CFLAGS} ${STREAM_CFLAGS} "${STREAM_DIR}/stream.c" -o "${stream}"
	(export ${stream_environment_variables[${i}]}; \
	 numactl ${stream_numactl_options[${i}]} \
		 ${stream})
	{ set +x; } 2>/dev/null
    done
}

main "$@"
