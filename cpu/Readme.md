## Project Instruction Updates:

1. Complete the function CPUScheduler() in vcpu_scheduler.c
2. If you are adding extra files, make sure to modify Makefile accordingly.
3. Compile the code using the command `make all`
4. You can run the code by `./vcpu_scheduler <interval>`
5. While submitting, write your algorithm and logic in this Readme.

### Algorithm

1. The scheduler checks if the current call is the first time the scheduler has been called. If false, skip to step 2.
    i. It intializes the domain pointers, number of domains, pcpu information, allocates memory for the vcpuInfoList.
2. For each domain, the scheduler collects the cpu time the vcpu used since the beginning.
    i. If this is not the first call to the scheduler, the vcpu usage over the previous iteration is calculated and stored.
    ii. The per pcpu usage over the previous iteration is also calculated.
3. If this is the first time the scheduler is called, it returns.
4. It checks the stability of the current workload distribution across the pcpus.
    i. The average and standard deviation of pcpu usages are calculated.
    ii. If the standard deviation is more than 5% of the average, the pinning is not stable.
5. If not stable, the scheduler will repin vcpus to pcpus. Else it will keep the pinning as is and returns.
6. Vcpu structures containing the domain pointers and the vcpu usage of the previous iteration are sorted based on the vcpu usages.
7. For each vpcu the following is done.
    i. If the vcpu usage over the previous iteration was 0, we break from the loop.
    ii. Based on the new mappings, the pcpu with the minimum workload is selected.
    iii. The corresponding vcpu map string is created and the vcpu is pinned to that pcpu.
    iv. The VCPU workload is added to the corresponding pcpu structure.
8. Scheduler is done, it returns.

### Logic

1. All the calculations are based on the vpcu and pcpu usages over the previous iteration.
2. The stability of the vcpu pinning is determined by checking if the standard deviation of pcpu usage across pcpus is less than 5% of the average.
3. If unstable, a greedy strategy is used to pin the vcpus to pcpus.
4. All the vcpus are sorted based on their usage over previous iterations.
5. The vcpus are sequentially allotted to the pcpu with the least workload in the current pinning. This is done until the first vcpu with zero usage is encountered. Since it does not make sense to move vcpus with zero uasge around, the re-pinning stops there.