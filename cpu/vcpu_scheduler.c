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

// Assuming that the number of pcpus and vcpus do not cross 100.
#define MAX_PCPUS 100
#define MAX_VCPUS 100

int is_exit = 0; // DO NOT MODIFY THIS VARIABLE

// A list of domain pointers.
virDomainPtr* domainPtrs = NULL;
virVcpuInfoPtr vcpuInfoList = NULL;
int numOfDomains = -1;
int numOfPcpus = -1;

// A list that is populated with the vcpu usages of this iteration.
unsigned long long vcpuTime[MAX_VCPUS];
// A list populated with the vcpu usages of the previous iteration.
unsigned long long vcpuPrevTime[MAX_VCPUS];
// A list populated with the pcpu usages of this iteration.
unsigned long long pcpuTime[MAX_PCPUS];

// Structure storing the domain pointer and the vcpu usage of this iteration.
typedef struct vcpuStr {
	virDomainPtr domainPtr;
	unsigned long long vcpuUsage;
} vcpuStr;

vcpuStr vcpuStrArray[MAX_VCPUS];

// Strucure storing the pcpu number and the assigned workload.
typedef struct pcpuStr {
	int pcpu;
	unsigned long long pcpuUsage;
} pcpuStr;

pcpuStr pcpuStrArray[MAX_PCPUS];

void CPUScheduler(virConnectPtr conn,int interval);
double calculateAverage();
double calculateStdDev(double ave);
int checkStability();


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

	// Get the total number of pCpus in the host
	signal(SIGINT, signal_callback_handler);

	while(!is_exit)
	// Run the CpuScheduler function that checks the CPU Usage and sets the pin at an interval of "interval" seconds
	{
		CPUScheduler(conn, interval);
		sleep(interval);
	}

	// Closing the connection
	virConnectClose(conn);
	return 0;
}

// Compare the usage across two vcpus. Used for sorting the vcpu structures.
int cmpVcpuStr(const void * a, const void * b)
{
	unsigned long long usageA = ((vcpuStr*)a)->vcpuUsage;
	unsigned long long usageB = ((vcpuStr*)b)->vcpuUsage;
	return usageB - usageA;
}

// Finding the pcpu allocated the minimum workload so far.
int findMinPcpuIndex()
{
	unsigned long long minIndex = 0;
	for (int i = 1; i < numOfPcpus; i++)
	{
		if (pcpuStrArray[i].pcpuUsage < pcpuStrArray[minIndex].pcpuUsage)
		{
			minIndex = i;
		}
	}

	return minIndex;
}

// Calculating the average usage of the pcpus in the previous iteration.
double calculateAverage()
{
	unsigned long long sum = 0;
	for (int i = 0; i < numOfPcpus; i++)
	{
		sum += pcpuTime[i];
	}
	double ave = ((double)sum)/((double)numOfPcpus);

	return ave;
}

// Calculating the standard deviation of pcpu usage in the previous iteration.
double calculateStdDev(double ave)
{
	double sumOfSq = 0;

	for (int i = 0; i < numOfPcpus; i++)
	{
		sumOfSq += pow(((double)pcpuTime[i] - ave),2);
	}
	
	double stdDev = pow((sumOfSq/((double)(numOfPcpus - 1))), 0.5);

	return stdDev;
}

// Checking the stability of the pcpus.
int checkStability()
{
	double ave = calculateAverage();
	double stdDev = calculateStdDev(ave);

	if (stdDev <= (0.05 * ave)) return 1;
	else return 0;
}

// Generate the vcpu to pcpu map byte based on the arguments.
int generateCpuMap(unsigned char** cpumap, int pcpu)
{

	int byteNum = pcpu/8;
	int byteOffset = pcpu%8;

	*cpumap = (unsigned char*)calloc(byteNum + 1, sizeof(unsigned char));
	
	for (int i = 0; i <= byteNum ;i++)
	{
		*cpumap[i] = (unsigned char)0;
	}
	*cpumap[byteNum] = (unsigned char)(1<<byteOffset);

	return byteNum + 1;
}

/* COMPLETE THE IMPLEMENTATION */
void CPUScheduler(virConnectPtr conn, int interval)
{
	// Flag that indicates if this is the first time the scheduler runs.
	int startUp = 0;

	printf("Scheduler called.\n");

	// Checking if this is the first time the scheduler is being run.
	if (domainPtrs == NULL)
	{
		startUp = 1;

		numOfDomains = virConnectListAllDomains(conn, &domainPtrs, VIR_CONNECT_LIST_DOMAINS_ACTIVE);
		if(numOfDomains == -1)
		{
			fprintf(stderr, "Error with virConnectListAllDomains function.\n");
			return;
		}
	}

	// Extracting the number of pcpus in the machine.
	if (numOfPcpus == -1)
	{
		virNodeInfoPtr resourceInfo = (virNodeInfoPtr)malloc(sizeof(virNodeInfo));
		if (virNodeGetInfo(conn, resourceInfo) != 0)
		{
			fprintf(stderr, "Error with virNodeGetInfo function.\n");
			return;
		}
		numOfPcpus = (int)(resourceInfo->cpus);
	}

	// Initializing the vcpuInfoList and vcpu arrays.
	if(vcpuInfoList == NULL)
	{
		vcpuInfoList = (virVcpuInfoPtr)malloc(sizeof(virVcpuInfo) * numOfDomains);
		
		for (int i = 0; i < MAX_VCPUS; i++)
		{
			vcpuPrevTime[i] = 0;
			vcpuTime[i] = 0;
		}
	}

	// Initilizing the pcpu arrays.
	for (int i = 0; i < MAX_PCPUS; i++)
	{
		pcpuTime[i] = 0;
		pcpuStrArray[i].pcpu = i;
		pcpuStrArray[i].pcpuUsage = 0;
	}
	
	for (int i = 0; i < numOfDomains; i++)
	{
		// Setting maxinfo to 1, because there is only be 1 vcpu per VM.
		if(virDomainGetVcpus(domainPtrs[i], &vcpuInfoList[i], 1, NULL, 0) == -1)
		{
			fprintf(stderr, "Error with virDomainGetVcpus() call.\n");
		}

		if (vcpuPrevTime[i] != 0){
			vcpuTime[i] = vcpuInfoList[i].cpuTime - vcpuPrevTime[i];
		}
		vcpuPrevTime[i] = vcpuInfoList[i].cpuTime;

		vcpuStrArray[i].domainPtr = domainPtrs[i];
		vcpuStrArray[i].vcpuUsage = vcpuTime[i];

		pcpuTime[vcpuInfoList[i].cpu] += vcpuTime[i];
	}

	// If it is the first time the scheduler is running, the scheduling logic is not executed.
	if (startUp == 0)
	{	
		int stableCheck = checkStability();
		
		// Execute if only not stable.
		if (stableCheck == 0)
		{
			// sort the vcpuStr array based on the vpcu usages.
			qsort(vcpuStrArray, numOfDomains, sizeof(vcpuStr), cmpVcpuStr);

			for (int i = 0; i < numOfDomains; i++)
			{
				if (vcpuStrArray[i].vcpuUsage == 0)break;

				// Find the pcpu with the minimum workload assigned to it.
				int minPcpuIndex = findMinPcpuIndex();
				
				unsigned char* cmap;
				int maplen = generateCpuMap(&cmap, pcpuStrArray[minPcpuIndex].pcpu);
				
				// Pin the vcpu that is assigned to the pcpu in consideration.
				if (virDomainPinVcpu(vcpuStrArray[i].domainPtr, 0, cmap, maplen) == -1){
					fprintf(stderr, "Error with virDomainPinVcpu() call.\n");
				}

				printf("Domain number: %u pinned to pcpu: %d\n", virDomainGetID(vcpuStrArray[i].domainPtr), pcpuStrArray[minPcpuIndex].pcpu);

				pcpuStrArray[minPcpuIndex].pcpuUsage += vcpuStrArray[i].vcpuUsage;
			}
		}
	}
}






