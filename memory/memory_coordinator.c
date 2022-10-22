#include<stdio.h>
#include<stdlib.h>
#include<libvirt/libvirt.h>
#include<math.h>
#include<string.h>
#include<unistd.h>
#include<limits.h>
#include<signal.h>
#define MIN(a,b) ((a)<(b)?a:b)
#define MAX(a,b) ((a)>(b)?a:b)

#define MAX_VMS 50

int is_exit = 0; // DO NOT MODIFY THE VARIABLE

// List of domain pointers
virDomainPtr* domainPtrs = NULL;

int numOfDomains = -1;
int statCollectPeriod = 0;

// Amount of free memory in the host.
unsigned long long hostUnusedMemory = 0;

// The minimum percentage of total size that can be free before coordinator is called to add memory.
unsigned long long unusedMinThresholdPercent = 15;
// The maximum percentage of total size that can be free before coordinator is called to release memory.
unsigned long long unusedMaxThresholdPercent = 30;
// The percentage of total size to increase the domain's memory by.
unsigned long long increaseAmountPercent = 15;
// The percentage of total size to decrease the domain's memory by.
unsigned long long decreaseAmountPercent = 20;

unsigned long long unusedMinThreshold;
unsigned long long unusedMaxThreshold;
unsigned long long increaseAmount;
unsigned long long decreaseAmount;

// In case we want to use static values instead of percentages of the entire memory size.
// unsigned long long unusedMinThreshold = 80 * 1024;
// unsigned long long unusedMaxThreshold = 200 * 1024;
// unsigned long long increaseAmount = 100 * 1024;
// unsigned long long decreaseAmount = 100 * 1024;

// Free memory which cannot be released in a domain.
unsigned long long minFreeDomain = 100 * 1024;
// Free memory which cannot be released from host
unsigned long long minFreeHost = 200 * 1024;

// Maximum memory to be allocated to a domain.
unsigned long long maxMemDomain = 2048 * 1024;
// This value is not really used, just to set the memory back to 512MB when a domain has more than 512MB and starts releasing memory to host.
unsigned long long baseMemoryAlloc = 512 * 1024;

// The amount of unused memory detected this iteration.
unsigned long long unusedMemory[MAX_VMS];
// The balloon size of the domain this iteration.
unsigned long long baloonValue[MAX_VMS];

// Flag set to indicate the need to increase the domain's memory.
int toIncrease[MAX_VMS];
// Flag set to indicate the need to decrease the domain's memory.
int toDecrease[MAX_VMS];
// Flag to indicate if the domain memory could be usable for deallocation this iteration.
int usedThisIter[MAX_VMS];

void MemoryScheduler(virConnectPtr conn,int interval);

/*
DO NOT CHANGE THE FOLLOWING FUNCTION
*/
void signal_callback_handler()
{
	printf("Caught Signal");
	is_exit = 1;
}

/*
DO NOT CHANGE THE FOLLOWING FUNCTION
*/
int main(int argc, char *argv[])
{
	virConnectPtr conn;

	if(argc != 2)
	{
		printf("Incorrect number of arguments\n");
		return 0;
	}

	// Gets the interval passes as a command line argument and sets it as the STATS_PERIOD for collection of balloon memory statistics of the domains
	int interval = atoi(argv[1]);
	
	conn = virConnectOpen("qemu:///system");
	if(conn == NULL)
	{
		fprintf(stderr, "Failed to open connection\n");
		return 1;
	}

	signal(SIGINT, signal_callback_handler);

	while(!is_exit)
	{
		// Calls the MemoryScheduler function after every 'interval' seconds
		MemoryScheduler(conn, interval);
		sleep(interval);
	}

	// Close the connection
	virConnectClose(conn);
	return 0;
}

// Attempting to extract required memory from domains and host.
unsigned long long extractMemory(unsigned long long amountRequested)
{
	unsigned long long required = amountRequested;
	unsigned long long amountDecreased;

	for (int i = 0; i<numOfDomains; i++)
	{
		unusedMaxThreshold = (unusedMaxThresholdPercent * baloonValue[i])/100;
		decreaseAmount = (decreaseAmountPercent * baloonValue[i])/100;

		if (usedThisIter[i] == 0){
			// Checking if the unused memory is above the required threshold and minimum free memory requirement
			if ((unusedMemory[i] >= unusedMaxThreshold) && (unusedMemory[i] >= minFreeDomain))
			{
				amountDecreased = MAX(MIN(MIN(decreaseAmount,required),unusedMemory[i]-minFreeDomain),0);
				// Subtracting the amount to be extracted from the rest of the domains/host
				required -= amountDecreased;
				if (amountDecreased != 0)
				{
					toDecrease[i] = 1;
					baloonValue[i] = baloonValue[i] - amountDecreased;
				}

				if (required == 0) break;
			}
			// Indicating that we can no longer use this domain to extract memory this iteration.
			usedThisIter[i] = 1;
		}
	}

	// Extracting memory from host if necessary.
	if (required > 0)
	{
		// The free memory of the host comes in bytes, not Kilobytes.
		// Checking if the total amount of free memory exceeds the minimum requirements of the host.
		if ((hostUnusedMemory/1024) > minFreeHost)
		{
			amountDecreased = MAX(MIN(required,(hostUnusedMemory/1024)-minFreeHost),0);
			required -= amountDecreased;
		}
	}

	return (amountRequested - required);

}

// Calculating the baloon values that need to be updated in this iteration by the coordinator.
void calculateBaloonValues()
{
	for (int i = 0; i<numOfDomains; i++)
	{
		increaseAmount = (baloonValue[i] * increaseAmountPercent)/100;

		if (toIncrease[i] == 1)
		{
			// Calculating the amount of memory that can be requested.
			unsigned long long requestMem = MIN(increaseAmount, maxMemDomain - baloonValue[i]);
			// If requestMem is 0, it means the VM has hit 2048 MB.
			if (requestMem == 0) continue;

			// Calculating the amount of memory that can be allocated.
			unsigned long long additionalMem = extractMemory(MIN(increaseAmount, requestMem));
			// If additional memory acquired is zero, we should not call the baloon driver.
			if (additionalMem != 0)
			{
				baloonValue[i] = baloonValue[i] + additionalMem;
			}
			else{
				toIncrease[i] = 0;
			}

			if (additionalMem < requestMem){
				// This means all VMs/hosts could not satisfy the additional memory req of this VM.
				break;
			}
		}
	}

	// Return memory to host if possible.
	for (int i = 0; i<numOfDomains; i++)
	{
		unusedMaxThreshold = (unusedMaxThresholdPercent * baloonValue[i])/100;
		decreaseAmount = (decreaseAmountPercent * baloonValue[i])/100;

		// Executed only if this domain has not already been allocated/de-allocated memory this iteration of the coordinator.
		if (usedThisIter[i] == 0)
		{
			// Memory is released only if total memory is more than 512MB and unused memory is more than the maximum threshold.
			if ((baloonValue[i] > baseMemoryAlloc) && (unusedMemory[i] >= unusedMaxThreshold))
			{
				unsigned long long amountDecreased = MIN(decreaseAmount, baloonValue[i]-baseMemoryAlloc);
				if (amountDecreased!=0)
				{
					toDecrease[i] = 1;

					baloonValue[i] = baloonValue[i] - decreaseAmount;
				}
			}

			usedThisIter[i] = 1;
		}
	}
}

/*
COMPLETE THE IMPLEMENTATION
*/
void MemoryScheduler(virConnectPtr conn, int interval)
{
	printf("Coordinator called.\n");

	// Checking if this is the first time the coordinator is being called.
	if (domainPtrs == NULL)
	{
	 	statCollectPeriod = interval;

		// Collecting the domain pointes.
		numOfDomains = virConnectListAllDomains(conn, &domainPtrs, VIR_CONNECT_LIST_DOMAINS_ACTIVE);
		if(numOfDomains == -1)
		{
			fprintf(stderr, "Error with virConnectListAllDomains function.\n");
			return;
		}

		for (int i = 0; i<numOfDomains; i++)
		{
			// Setting the memory statistics collection period for the domain pointers.
			if (virDomainSetMemoryStatsPeriod(domainPtrs[i], statCollectPeriod, VIR_DOMAIN_AFFECT_LIVE) == -1)
			{
				fprintf(stderr, "Error with virDomainSetMemoryStatsPeriod function.\n");
				return;
			}
		}
	}

	// Collecting the free meemory statistic of the host.
	hostUnusedMemory = virNodeGetFreeMemory(conn);

	int nr_stats = 7;
	// Allocating the array of structures to populate the mempory statistics.
	virDomainMemoryStatPtr stats = (virDomainMemoryStatPtr)malloc(sizeof(virDomainMemoryStatStruct) * nr_stats);
	for (int i = 0; i<numOfDomains; i++)
	{
		toIncrease[i] = 0;
		toDecrease[i] = 0;
		usedThisIter[i] = 0;

		nr_stats = 7;
		// Collecting memory stats of the domain.
		nr_stats = virDomainMemoryStats(domainPtrs[i], stats, nr_stats, 0);

		if (nr_stats == -1)
		{
			fprintf(stderr, "Error with virDomainMemoryStats function.\n");
			return;
		}

		int statsCollected = 0;

		for (int j = 0; j<nr_stats; j++)
		{
			if (stats[j].tag == 4) 
			{
				unusedMemory[i] = stats[j].val;
				statsCollected++;
			}
			else if (stats[j].tag == 6)
			{
				baloonValue[i] = stats[j].val;
				statsCollected++;
			}
		}

		if (statsCollected != 2)
		{
			printf("Unused Memory and/or Baloon Size could not be collected for: Domain %u. Not running memory scheduler this iteration.\n",virDomainGetID(domainPtrs[i]));
			return;
		}
		else
		{
			unusedMinThreshold = (unusedMinThresholdPercent * baloonValue[i])/100;

			// Checking if the unused amount is less than the maximum threshold.
			if (unusedMemory[i] <= unusedMinThreshold)
			{
				toIncrease[i] = 1;
				usedThisIter[i] = 1;
			}
		}
	}

	free(stats);

	calculateBaloonValues();
	
	// Allocating baloon values
	for (int i = 0; i < numOfDomains; i++)
	{
		if ((toIncrease[i] == 1) || (toDecrease[i] == 1))
		{
			// Setting the baloon value of the domain according to need.
			if (virDomainSetMemory(domainPtrs[i], baloonValue[i]) == -1)
			{
				fprintf(stderr, "Error with virDomainSetMemory function.\n");
				return;
			}

			if (toIncrease[i] == 1)
			{
				printf("Increased the memory allocated to Domain%u to %llu\n", virDomainGetID(domainPtrs[i]),baloonValue[i]/1024);
			}
			if (toDecrease[i] == 1)
			{
				printf("Decreased the memory allocated to Domain%u to %llu\n", virDomainGetID(domainPtrs[i]),baloonValue[i]/1024);
			}
		}
	}
}
