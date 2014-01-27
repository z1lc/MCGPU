#include <stdlib.h>
#include <stdio.h>
#include <time.h>
#define N 50000
#define BLOCK_SIZE 512

//Given two indices in an array (representing atoms),
//calculate their product (potential energy),
//and store in energies array.
//Parallel
__global__ void calcEnergyParallel(int *atoms, int numAtoms, int *energies, int numEnergies)
{
	int atom1 = blockIdx.x, atom2 = blockIdx.y * blockDim.x + threadIdx.x,
		energyIdx;
	
	if (atom2 < numAtoms && atom2 > atom1)
	{
		energyIdx = gridDim.x * atom1 + atom2 - (blockIdx.x + 1) * (blockIdx.x + 2) / 2;
		energies[energyIdx] = atoms[atom1] * atoms[atom2];
	}
}

//Given two indices in an array (representing atoms),
//calculate their product (potential energy),
//and store in energies array.
//Serial
void calcEnergySerial(int *atoms, int numAtoms, int *energies, int numEnergies)
{
	int i, j, k;
	
	for (i = 0; i < numAtoms; i++)
	{
		for (j = 0; j < numAtoms; j++)
		{
			if (j > i)
			{
				k = N * i + j - (i + 1) * (i + 2) / 2;
				energies[k] = atoms[i] * atoms[j];
			}
		}
	}
}

int main()
{
	clock_t S_TIME, P_TIME;
	int *atomsHost, *atomsDevice, *energiesHost, *energiesDevice, gridYDim = 1, blockXDim = N;
	unsigned long int totalEnergy, atomsSize, energiesSize;
	
	atomsSize = N * sizeof(int);
	energiesSize = sizeof(int) * N * (N - 1) / 2;
	
	atomsHost = (int*) malloc(atomsSize);
	energiesHost = (int*) malloc(energiesSize);
	
	int i;
	for (i = 0; i < N; i++)
	{
		atomsHost[i] = i;
	}
	
	for (i = 0; i < energiesSize / sizeof(int); i++)
	{
		energiesHost[i] = 0;
	}
	
	//Serial Run
	S_TIME = clock();
	calcEnergySerial(atomsHost, N,energiesHost, energiesSize / sizeof(int));

	totalEnergy = 0;
	for (i = 0; i < energiesSize / sizeof(int); i++)
	{
		//totalEnergy += energiesHost[i];
	}
	
	printf("Serial: Total Energy for %u atoms is %u Pseudo-Joules.\n", N, totalEnergy);
	S_TIME = clock() - S_TIME;
	
	//Reset energiesHost
	for (i = 0; i < energiesSize / sizeof(int); i++)
	{
		energiesHost[i] = 0;
	}

	//Parallel Run
	P_TIME = clock();
	if (N > BLOCK_SIZE)
	{
		gridYDim = N / BLOCK_SIZE + 1;
		blockXDim = BLOCK_SIZE;
	}
	dim3 gridDim(N, gridYDim, 1);
	dim3 blockDim(blockXDim, 1, 1);

	cudaMalloc(&atomsDevice, atomsSize);
	cudaMalloc(&energiesDevice, energiesSize);
	
	cudaMemcpy(atomsDevice, atomsHost, atomsSize, cudaMemcpyHostToDevice);
	cudaMemcpy(energiesDevice, energiesHost, energiesSize, cudaMemcpyHostToDevice);
	
	//N blocks of N threads (every atom pair)
	calcEnergyParallel<<<gridDim, blockDim>>>(atomsDevice, N, energiesDevice, energiesSize / sizeof(int));

	cudaMemcpy(energiesHost, energiesDevice, energiesSize, cudaMemcpyDeviceToHost);

	totalEnergy = 0;
	for (i = 0; i < energiesSize / sizeof(int); i++)
	{
		//printf("%u: %u\n", i, energiesHost[i]);
		//totalEnergy += energiesHost[i];
	}
	
	printf("Parallel: Total Energy for %u atoms is %u Pseudo-Joules.\n", N, totalEnergy);
	P_TIME = clock() - P_TIME;

	printf("The parallel code runs %fx as fast as the serial version.\n", (float) S_TIME / (float) P_TIME);
	
	return 0;
}