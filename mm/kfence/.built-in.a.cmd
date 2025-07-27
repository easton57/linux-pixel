cmd_mm/kfence/built-in.a := rm -f mm/kfence/built-in.a;  printf "mm/kfence/%s " core.o report.o | xargs aarch64-linux-gnu-ar cDPrST mm/kfence/built-in.a
