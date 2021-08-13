#!/system/bin/sh

STARTTIME=$(date +%s)
device=ONEPLUS
platform=ONEPLUS
bimc_scaling_freq_string="1804800 1555200 1296000 1017600 768000 681600 547200 412800 300000 200000"

display_freq() {
  if [ -f /proc/clk/bimc_clk/measure ]; then
    echo "BIMC cur_freq: " $(< /proc/clk/bimc_clk/measure)
  elif [ -f /proc/clk/bimc_clk/clk_measure ]; then
    echo "BIMC cur_freq: " $(< /proc/clk/bimc_clk/clk_measure)
  elif [ -f /proc/clk/measure_only_bimc_clk/clk_measure ]; then
    # Napali
    echo "BIMC cur_freq: " $(< /proc/clk/measure_only_bimc_clk/clk_measure)
  elif [ -f /proc/clk/measure_only_mccc_clk/clk_measure ]; then
    # Hana
    echo "BIMC cur_freq: " $(< /proc/clk/measure_only_mccc_clk/clk_measure)
  else
    echo "BIMC measure unsupported"
  fi
}

bimc_freq_switch() {
  ## don't turn off thermal-engine, otherwise thermal reset will be triggered easily. #stop thermal-engine
  for REQ_KHZ in ${FREQ_LIST}; do
    if [ -f /proc/aop_send_message ]; then
        # echo "BIMC req_freq: ${REQ_KHZ}"
        echo "{class:ddr, res:fixed, val: $((${REQ_KHZ}/1000))}" > /proc/aop_send_message
    elif [ -f /proc/rpm_send_msg/message ]; then
        if [ $device = "Things" ] && [ $platform = "msm8x53" ];then
             echo "MSM8x53 LAT Target"
        else
              echo 1 > /proc/msm-bus-dbg/shell-client/mas
              echo 512 > /proc/msm-bus-dbg/shell-client/slv 
        fi      
        echo "active clk2 0 1 max ${REQ_KHZ}" > /proc/rpm_send_msg/message
        ## if clearing vote, set max to 0 otherwise vote for a extra large freq value
        if [ "${REQ_KHZ}" = "0" ]; then
        echo "clearing override votes"
        echo 0 > /proc/msm-bus-dbg/shell-client/ib
        else
        echo "BIMC req_freq: ${REQ_KHZ}"
        # echo 7200000000 > /d/msm-bus-dbg/shell-client/ib
        # echo $((8000*900000)) > /d/msm-bus-dbg/shell-client/ib
        echo $((7200000000)) > /proc/msm-bus-dbg/shell-client/ib
        fi
        echo 1 > /proc/msm-bus-dbg/shell-client/update_request
    else
      echo "no supported clock switching nodes available on this target"
    fi

    # display_freq
    sleep $interval
  done
}

loop_bimc_freq_switch() {
  while [ 1 ] ; do
    bimc_freq_switch
  done
}

## entry ###

## get the interval
opt=$1
if [ "$opt" = "-i" ]; then
  shift
  interval=$1
  shift
  opt=$1
  echo "switching interval set to $interval"
else
  interval=0.2
  echo "switching interval set to default 0.2 sec"
fi

if [ "$opt" = "-d" ]; then
  shift
  device=$1
  shift
  shift
  platform=$1
fi

echo "support bimc clock switch in $platform"
if [ $platform = "trinket" ]; then
  bimc_scaling_freq_string="1804800 1555200 1296000 1017600 768000 681600 547200 412800 300000 200000"
elif [ $platform = "sdm710" ]; then
  bimc_scaling_freq_string="1804800 1555200 1296000 1017600 768000 681600 547200 412800 300000 200000"
elif [ $platform = "sdm6125" ]; then
  bimc_scaling_freq_string="1804800 1555200 1296000 1017600 768000 681600 547200 412800 300000 200000"
elif [ $platform = "msmnile" ]; then
  bimc_scaling_freq_string="2092800 1804800 1555200 1353600 1017600 768000 681600 547200 451200 300000 200000"
elif [ $platform = "lito" ]; then
  bimc_scaling_freq_string="2092800 1804800 1555200 1353600 1017600 768000 681600 547200 451200 300000 200000"
elif [ $platform = "kona" ]; then
  bimc_scaling_freq_string="2736000 2092800 1804800 1555200 1353600 1017600 768000 681600 547200 451200 300000 200000"
else
  bimc_scaling_freq_string="1804800 1555200 1296000 1017600 768000 681600 547200 412800"
fi

echo "Support switch freqs: $bimc_scaling_freq_string"

RANDOM=$$$(date +%s)
random_bimc_freq_switch() {
   # Seed random generator
   randnum=$RANDOM
   arrlen=$#
   randindex=$(($randnum % $arrlen))
   # randomly select the frequency from the list
   i=0
   for var in $bimc_scaling_freq_string
   do
      if [ $i -eq $randindex ]; then
        FREQ_LIST=$var
        bimc_freq_switch
      fi
      i=$(($i+1))
   done
   #FREQ_LIST=200000
   #bimc_freq_switch
}

loop_random_bimc_freq_switch() {
  while [ 1 ] ; do
    random_bimc_freq_switch $bimc_scaling_freq_string
  done
}

## some targets require an enable message to be send before starting clock switching
if [ -f /proc/rpm_send_msg/enable ]; then
  echo 3735928559  > /proc/rpm_send_msg/enable
  echo 0 > /proc/msm-bus-dbg/shell-client/update_request
  echo "enabled rpm messages"
fi

loop_random_bimc_freq_switch
