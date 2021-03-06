#!/usr/bin/bash
################## Get the list of all tests #############
lsf_job_list=( )
bsub_resource_string="select[(os== lin && type == X86_64 && (osdistro == rhel || osdistro == centos) && (osver== ws7))] rusage[mem=16000]"
echo $bsub_resource_string
# source jenkins_autorun_csim.sh , to verify functionality
launch_dir=$(pwd)
cd $launch_dir
source jenkins_autorun_csim.sh
cd $launch_dir
echo "----------------CSIM-INFO-----------------"
source check_csim_status.sh
echo "Done with CSIM func tests launching synthesis"
script_name="Makefile"

echo "Clean all logs created by C-SIM and proceed to Synthesis."
source clean_all.sh

echo "=================================================="
echo "Collecting info for launching tests"
echo "=================================================="

all_tests=$(find $(pwd) -type f -name "run_hls.tcl")
all_tests_dirs=$(dirname $all_tests)
no_of_tests=$(echo $all_tests | wc -w)
echo "=================================================="
echo "Summary of tests found:"
echo "Total number of tests found : $no_of_tests"
echo "List of tests found:"
for test in $all_tests
do
    echo $test
done

echo "=================================================="
echo "Setting up test environment ..."

echo "--Vivado_hls used :"
which vivado_hls
echo "=================================================="

echo "Remove all old test run logs ..."
for tdir in $all_tests_dirs
do
    cd $tdir
    rm -rf vivado_hls.log
done
echo "Cleaned all old run logs ..."

cd $launch_dir
for tdir in $all_tests_dirs
do
	echo "-------------------------------------------"
	echo "Entering Test Directory : $tdir"
	echo "-------------------------------------------"
	cd $tdir
	# Create a script file to launch for bsub job
	rm -rf start_bsub.sh
	echo "#!/usr/bin/bash" >> start_bsub.sh

	#echo "which bash" >> start_bsub.sh
	echo "make run CSIM=1 CSYNTH=1 COSIM=1 XPART={xcu200-fsgd2104-2-e}" >> start_bsub.sh
	bsub -env "all" -J $tdir -cwd $tdir -o vivado_hls.log -R "select[(os== lin && type == X86_64 && (osdistro == rhel || osdistro == centos) && (osver== ws7))] rusage[mem=16000]" bash start_bsub.sh
	wait_string="ended($tdir)"
	lsf_job_list+=( $wait_string)
	echo "-------------------------------------------"
	echo "Launched test : $tdir/$script_name"
	echo "-------------------------------------------"
done

# Wait for all test jobs to finish
    echo "-------------------------------------------"
    echo "Waiting for all Jobs to finished .... "
    echo "-------------------------------------------"
    for jobb in ${lsf_job_list[@]}; do
	echo "--Waiting on bsub COSIM job: $jobb"
	bwait -w $jobb
    done


echo "Collect all test stats ..."
total_test=0;
passed_test=0;
failed_test=0;
failed_list=();
for tdir in $all_tests_dirs
do
    total_test=$((total_test+1))
    cd $tdir 
    res=$(grep -i "fail" vivado_hls.log)
    echo "--------------------------------------------"
    echo "Test : $tdir"
    if [ -z "$res" ]
    then
        echo "Test Passed ..."
        passed_test=$((passed_test+1))
    else
        echo "Test Failed ..."
        failed_test=$((failed_test+1))
        failed_list=(${failed_list[@]} $tdir)
    fi
    echo "--------------------------------------------"
done
echo "======================================================"
echo "======================================================"
echo "-----------------Test Result Summary------------------"
echo "Total Number of tests: $total_test"
echo "Number of tests PASSED: $passed_test"
echo "Number of tests FAILED: $failed_test"
echo "  "

## Check if there are any failures in tests
if [ $failed_test -gt 0 ]; then
	echo "The list of failed tests:"
	for ftest in ${failed_list[@]}
	do
	    echo "$ftest"
	done
## exit run and pass status 1 to jenkins
       echo "Library builtinTests Failed !"
	exit 1 
fi


## Check if All tests were ran
if [ $total_test -ne $passed_test ]; then
	echo "All tests were not Run, some test missed, TEST RUN FAILURE !"
	exit 1 
fi

echo "======================================================"
echo "======================================================"


cd $launch_dir
source check_csim_status.sh
source check_csynth_status.sh
source check_cosim_status.sh
