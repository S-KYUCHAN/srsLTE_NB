# CMake generated Testfile for 
# Source directory: /home/syssec/Kyuchan/srsLTE/lib/test/upper
# Build directory: /home/syssec/Kyuchan/srsLTE/build/lib/test/upper
# 
# This file includes the relevant testing commands required for 
# testing this directory and lists subdirectories to be tested as well.
add_test(rlc_am_data_test "rlc_am_data_test")
add_test(rlc_am_control_test "rlc_am_control_test")
add_test(rlc_am_test "rlc_am_test")
add_test(rlc_am_stress_test "rlc_stress_test" "--mode=AM" "--loglevel" "1" "--sdu_gen_delay" "250")
add_test(rlc_um_stress_test "rlc_stress_test" "--mode=UM" "--loglevel" "1")
add_test(rlc_tm_stress_test "rlc_stress_test" "--mode=TM" "--loglevel" "1" "--random_opp=false")
add_test(rlc_um_data_test "rlc_um_data_test")
add_test(rlc_um_test "rlc_um_test")
