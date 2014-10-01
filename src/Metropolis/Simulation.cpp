/*
	Driver for the simulation. Takes in a SimulationArgs object and creates the
	the necessary Box type, state file output path, etc.

	Author: Nathan Coleman
	Created: February 21, 2014
	
	-> February 26, by Albert Wallace
	-> March 28, by Joshua Mosby
	-> April 21, by Nathan Coleman
*/

#include <string>
#include <iostream>
#include <fstream>
#include <time.h>
#include <stdio.h>

#include "Simulation.h"
#include "SimulationArgs.h"
#include "Box.h"
#include "Metropolis/Utilities/MathLibrary.h"
#include "Metropolis/Utilities/Parsing.h"
#include "SerialSim/SerialBox.h"
#include "SerialSim/SerialCalcs.h"
#include "ParallelSim/ParallelCalcs.h"
#include "Utilities/FileUtilities.h"

#define RESULTS_FILE_DEFAULT "run"
#define RESULTS_FILE_EXT ".results"

//Constructor & Destructor
Simulation::Simulation(SimulationArgs simArgs)
{
	args = simArgs;

	stepStart = 0;

	if (simArgs.simulationMode == SimulationMode::Parallel)
		box = ParallelCalcs::createBox(args.filePath, args.fileType, &stepStart, &simSteps);
	else
		box = SerialCalcs::createBox(args.filePath, args.fileType, &stepStart, &simSteps);

	if (box == NULL)
	{
		std::cerr << "Error: Unable to initialize simulation Box" << std::endl;
		exit(EXIT_FAILURE);
	}
	else
	{
		std::cout << "Using seed: " << box->environment->randomseed << std::endl;
		seed(box->environment->randomseed);
	}

	if (args.stepCount > 0)
		simSteps = args.stepCount;
}

Simulation::~Simulation()
{
	if(box != NULL)
	{
		delete box;
		box = NULL;
	}
}

//Utility
void Simulation::run()
{
	std::cout << "Simulation Name: " << args.simulationName << std::endl;
	//declare variables common to both parallel and serial
	Molecule *molecules = box->getMolecules();
	Environment *enviro = box->getEnvironment();
	
	Real oldEnergy = 0, currentEnergy = 0;
	Real newEnergyCont, oldEnergyCont;
	Real  kT = kBoltz * enviro->temp;
	int accepted = 0;
	int rejected = 0;

	clock_t startTime, endTime;
    startTime = clock();
	
	//calculate old energy
	if (oldEnergy == 0)
	{
		if (args.simulationMode == SimulationMode::Parallel)
		{
			oldEnergy = ParallelCalcs::calcSystemEnergy(box);
		}
		else
		{
			oldEnergy = SerialCalcs::calcSystemEnergy(molecules, enviro);
		}
	}
	
	std::cout << std::endl << "Running " << simSteps << " steps" << std::endl << std::endl;
	
	//determine where we want the state file to go
	std::string baseStateFile = "";	
	if (!args.simulationName.empty())
	{
		baseStateFile.append(args.simulationName);
	}
	else
	{
		baseStateFile.append("untitled");
	}
	
	for(int move = stepStart; move < (stepStart + simSteps); move++)
	{
		if (args.statusInterval > 0 && (move - stepStart) % args.statusInterval == 0)
		{
			std::cout << "Step " << move << ":\n--Current Energy: " << oldEnergy << std::endl;	

		}
		
		if (args.stateInterval > 0 && move > stepStart && (move - stepStart) % args.stateInterval == 0)
		{
			std::cout << std::endl;
			saveState(baseStateFile, move);
			std::cout << std::endl;
		}
		
		int changeIdx = box->chooseMolecule();
		
		if (args.simulationMode == SimulationMode::Parallel)
		{
			oldEnergyCont = ParallelCalcs::calcMolecularEnergyContribution(box, changeIdx);
		}
		else
		{
			oldEnergyCont = SerialCalcs::calcMolecularEnergyContribution(molecules, enviro, changeIdx);
		}
			
		box->changeMolecule(changeIdx);
		
		if (args.simulationMode == SimulationMode::Parallel)
		{
			newEnergyCont = ParallelCalcs::calcMolecularEnergyContribution(box, changeIdx);
		}
		else
		{
			newEnergyCont = SerialCalcs::calcMolecularEnergyContribution(molecules, enviro, changeIdx);
		}
		
		bool accept = false;
		
		if(newEnergyCont < oldEnergyCont)
		{
			accept = true;
		}
		else
		{
			Real x = exp(-(newEnergyCont - oldEnergyCont) / kT);
			
			if(x >= randomReal(0.0, 1.0))
			{
				accept = true;
			}
			else
			{
				accept = false;
			}
		}
		
		if(accept)
		{
			accepted++;
			oldEnergy += newEnergyCont - oldEnergyCont;
		}
		else
		{
			rejected++;
			//restore previous configuration
			box->rollback(changeIdx);
		}
	}

	endTime = clock();
    double diffTime = difftime(endTime, startTime) / CLOCKS_PER_SEC;

	std::cout << "Step " << (stepStart + simSteps) << ":\r\n--Current Energy: " << oldEnergy << std::endl;
	currentEnergy = oldEnergy;

	// Save the final state of the simulation
	if (args.stateInterval >= 0)
	{
		saveState(baseStateFile, (stepStart + simSteps));
	}
	
	std::cout << std::endl << "Finished running " << simSteps << " steps" << std::endl;
	std::cout << "Final Energy: " << currentEnergy << std::endl;
	std::cout << "Run Time: " << diffTime << " seconds" << std::endl;
	std::cout << "Accepted Moves: " << accepted << std::endl;
	std::cout << "Rejected Moves: " << rejected << std::endl;
	std::cout << "Acceptance Ratio: " << 100.0 * accepted / (accepted + rejected) << '\%' << std::endl;

	std::string resultsName;
	if (args.simulationName.empty())
		resultsName = RESULTS_FILE_DEFAULT;
	else
		resultsName = args.simulationName;
	resultsName.append(RESULTS_FILE_EXT);

	// Save the simulation results.
	std::ofstream resultsFile;
	resultsFile.open(resultsName.c_str());

	resultsFile << "######### MCGPU Results File #############" << std::endl;
	resultsFile << "[Information]" << std::endl;
	resultsFile << "Timestamp = " << currentDateTime() << std::endl;
	if (!args.simulationName.empty())
		resultsFile << "Simulation-Name = " << args.simulationName << std::endl;

	if (args.simulationMode == SimulationMode::Parallel)
		resultsFile << "Simulation-Mode = GPU" << std::endl;
	else
		resultsFile << "Simulation-Mode = CPU" << std::endl;
	resultsFile << "Starting-Step = " << stepStart << std::endl;
	resultsFile << "Steps = " << simSteps << std::endl;
	resultsFile << "Molecule-Count = " << box->environment->numOfMolecules << std::endl << std::endl;
	resultsFile << "[Results]" << std::endl;
	resultsFile << "Final-Energy = " << currentEnergy << std::endl;
	resultsFile << "Run-Time = " << diffTime << " seconds" << std::endl;
	resultsFile << "Accepted-Moves = " << accepted << std::endl;
	resultsFile << "Rejected-Moves = " << rejected << std::endl;
	resultsFile << "Acceptance-Rate = " << 100.0f * accepted / (float) (accepted + rejected) << '\%' << std::endl;

	resultsFile.close();
	writePDB(enviro, molecules);

}

void Simulation::saveState(const std::string& baseFileName, int simStep)
{
	StateScanner statescan = StateScanner("");
	std::string stateOutputPath = baseFileName;
	std::string stepCount;

	if (!toString<int>(simStep, stepCount))
		return;

	stateOutputPath.append("_");
	stateOutputPath.append(stepCount); //add the step number to the name of the output file
	stateOutputPath.append(".state");

	std::cout << "Saving state file " << stateOutputPath << std::endl;

	statescan.outputState(box->getEnvironment(), box->getMolecules(), box->getMoleculeCount(), simStep, stateOutputPath);
}

int Simulation::writePDB(Environment sourceEnvironment, Molecule * sourceMoleculeCollection)
{
	//determine PDB file path
	std::string pdbName;
	if (args.simulationName.empty())
		pdbName = RESULTS_FILE_DEFAULT;
	else
		pdbName = args.simulationName;
	pdbName.append(".pdb");
	
	
	std::ofstream pdbFile;
	
	pdbFile.open(pdbName.c_str());

	
	int numOfMolecules = sourceEnvironment.numOfMolecules;
	pdbFile << "REMARK Created by MCGPU" << std::endl;
	
	for (int i = 0; i < numOfMolecules; i++)
	{
		Molecule currentMol = sourceMoleculeCollection[i];    	
        for (int j = 0; j < currentMol.numOfAtoms; j++)
        {
			Atom currentAtom = currentMol.atoms[j];
			pdbFile.setf(std::ios_base::left,std::ios_base::adjustfield);
			pdbFile.width(6);
			pdbFile << "ATOM";
			pdbFile.setf(std::ios_base::right,std::ios_base::adjustfield);
			pdbFile.width(5);
			pdbFile << currentAtom.id + 1;
			pdbFile.width(3); // change from 5
			pdbFile << currentAtom.name;
			pdbFile.width(6); // change from 4
			pdbFile << "UNK";
			pdbFile.width(6);
			pdbFile << i + 1;
			pdbFile.setf(std::ios_base::fixed, std::ios_base::floatfield);
			pdbFile.precision(3);
			pdbFile.width(12);
			pdbFile << currentAtom.x;
			pdbFile.width(8);
			pdbFile << currentAtom.y;
			pdbFile.width(8);
			pdbFile << currentAtom.z << std::endl;
        }
        pdbFile << "TER" << std::endl;
    }
    pdbFile << "END" << std::endl;
    pdbFile.close();

	return 0;
}
const std::string Simulation::currentDateTime()
{
    time_t     now = time(0);
    struct tm  tstruct;
    char       buf[80];
    tstruct = *localtime(&now);
    // Visit http://en.cppreference.com/w/cpp/chrono/c/strftime
    // for more information about date/time format
    strftime(buf, sizeof(buf), "%Y-%m-%d %X", &tstruct);

    return buf;
}