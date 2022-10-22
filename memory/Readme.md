
## Project Instruction Updates:

1. Complete the function MemoryScheduler() in memory_coordinator.c
2. If you are adding extra files, make sure to modify Makefile accordingly.
3. Compile the code using the command `make all`
4. You can run the code by `./memory_coordinator <interval>`

### Notes:

1. Make sure that the VMs and the host has sufficient memory after any release of memory.
2. Memory should be released gradually. For example, if VM has 300 MB of memory, do not release 200 MB of memory at one-go.
3. Domain should not release memory if it has less than or equal to 100MB of unused memory.
4. Host should not release memory if it has less than or equal to 200MB of unused memory.
5. While submitting, write your algorithm and logic in this Readme.

### Algorithm:

1. If the coordinator is called for the first time, implement the following substeps. Else skip to step 2.
    i. Collect the domain pointer information, and set the memory statistcs collection perriod to the interval of the coordinator.
2. Collect the free memory statistic of the host.
3. Collect the memory statistcs of each domain.
    i. Check if the unused memory and baloon value statistics are available for the domain. Else the coordinator returns.
    ii. Check if the unused memory of the domain is less than the threshold (unushedMinThresholdPercent (15%) of the total size of the domain). Set the flag toIncrease if true.
4. For each domain that has the toIncrease flag set:
    i. Calculate the maximum amount of memory it can request (this is the minimum of increaseAmountPercent (15%) of total memory and memory required to grow to 2048KB).
    ii. Go to step 5 to extract above calculated memory from other domains/host.
    iii. If amount of memory extracted for a domain is less than requested memory, go to step 7. No more memory can be extracted from domains/host this iteration of coordinator.
    iv. After exiting the loop, go to step 7.
5. For each domain that does not have toIncrease flag set, and has not been evaluated for memory extraction in this iteration of the coordintaor:
    i. Check if unused memory of the domain is greater than the threshold (unusedMaxThresholdPercent (30%) of the total size) and the unused size is greater than the minimumm required for a domain (100MB). If true, continue. Else move back to step 5 (continue the loop).
    ii. Calculate the maximum amount of memory that can be extracted from the domain. This is the minimum of the following three values: decreaseAmountPercent (20%) of total size, required amount to be extracted and the total unused memory left before the unused size reaches the minimum requirement (100MB).
    iii. Reduce the required amount to be extracted from the other domains/host by the value calculated in Step 5.ii.
    iv. Move to step 6.
6. If there is still memory required to be extracted, continue. Else move back to step 4.
    i. Check if the amount of free memory in host is more than the minimum requirement for the host (200MB). If false, return to step 4.
    ii. Calculate the amount of memory that can be extracted from host. This is minimum of two values, the required amount and the free memory extractable left before the free memory size hits the minimum requirement (200MB).
    iii. Reduce the required amount to be extracted by the value calculated in Step 6.ii.
    iv. Return to step 4.
7. For every domain that does not have the toIncrease flag set, and has not been evaluated for memory extraction in this iteration of the coordinator:
    i. Check if the domain has a size bigger than the intially allocated memory (512MB) and if the unused memory of the domain is greater than unusedMaxThresholdPercent (30%) of the total size. If false, return to step 7 (continue the loop).
    ii.  Decrease the baloon size of the corresponding domain by decreaseAmountPercent (20%) of the total size. 
8. Set the domain memory to the baloon values if the size of the domain memory has to be changed (allocation/reclaim).


### Logic:

1. The coordinator decides to allocate and release memory from domains based on percentages of thte total size of the domain's memory. The reasoning behind this is, static values might be too big or too small with respect to the total size of the domain's memory.
2. In each iteration of the coordinator, the domains' unused memory sizes are compared to the total size. If it is less than 15% of the size, the coordinator will try to allocate it more memmory. It tries to allocate the minimum of (15% of the total size) and (2048MB - total size).
3. To extract the memory required for the domain in consideration, the coordinator will evaluate other domains. If a domain has more than 30% of its total memory unused, and has more than 100MB free memory, it will be able to release a minimum of (20% of its total size) and (current unused memory - 100MB). In case the memory required to be extracted is lesser than the value calculated previously, only that much is extracted.
4. Note that when a domain has already been evaluated for memory extraction, it will not be evaluated again in the same iteration of the coordinator.
5. If the amount to be extracted is still not satisfied completely, the coordinator turns to the host. If the host has more than 200MB free, it will be able to give a minimum of the memory required by the domain and (free memory - 200MB).
6. Finally, if there is a domain that has not been evaluated and has more than 30% of its memory unused, it will release a minimum of (20% of total size) and the amount of memory it needs to release to reach a total size of 512MB. 

NOTE: The threshold (15% of total size) that determines whether the memory of the domain should be expanded, and the amount it is expanded by (15% of total size) has been chosen such that even if the memory consumption rate gets slower after it has been allotted, the memory will not be returned immediately in the next iteration. For this the threshold to determine if memory can be released was chosen accordingly, i.e., 30% of total size.