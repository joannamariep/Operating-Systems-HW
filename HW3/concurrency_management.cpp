#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <sys/wait.h>
#include <semaphore.h>
#include <errno.h>
#include <assert.h>
#include <fcntl.h>
#include <time.h>
#include <iostream>
#include <vector>
#include <sstream>

using namespace std;

#define SHARED_MEMORY_MUTEX             "JP_Shared_Memory_Mutex"
#define BRIDGE_OVERWEIGHT_LOCK_GUARD    "JP_BridgeOverweightLockGuard"

#define SHM_SZ    2048
#define MAX_WAITING_VEHICLES 100

// Helper method to simulate time delay
void Delay(double delayInSeconds)
{
    clock_t before = clock();
    while (true)
    {
        clock_t now = clock();
        double time = (now - before) / (double) CLOCKS_PER_SEC;

        // Break out of loop, if approximately one ms has elapsed;
        if (time >= delayInSeconds)
        {
            break;
        }
    }
}

struct BridgeStat
{
    int waitingVehicles;
    long currentWeight;
};

class ConcurrencyManager
{
    int sharedMemorySegmentId;
public:
    sem_t * sharedMemoryMutex;
    void * sharedMemorySegment;
    sem_t * overweightLockGuard;

    ConcurrencyManager()
    {
        sharedMemorySegment = NULL;
        sharedMemoryMutex = SEM_FAILED;
        overweightLockGuard = SEM_FAILED;
        sharedMemorySegmentId = -1;
    }

    int Initialize()
    {   
        // Initalize shared memory mutex
        sharedMemoryMutex = sem_open(SHARED_MEMORY_MUTEX, O_CREAT, 0600, 1);

        if (sharedMemoryMutex == SEM_FAILED)
        {
            return -1;
        }

        // Initialize overweight lock guard
        overweightLockGuard = sem_open(BRIDGE_OVERWEIGHT_LOCK_GUARD, O_CREAT, 0600, 0);

        if (overweightLockGuard == SEM_FAILED)
        {
            return -1;
        }

        cout << "Initialized shared memory mutex" << endl;

        // Initialize shared memory segment
        key_t key = 987612345;
        sharedMemorySegmentId = shmget(key, SHM_SZ, 0600 | IPC_CREAT);

        cout << "Segment Id: " << sharedMemorySegmentId << endl;

        if (sharedMemorySegmentId < 0)
        {
            return sharedMemorySegmentId;
        }

        cout << "Initialized shared memory segment" << endl << endl;

        // Retrieve the shared segment
        sharedMemorySegment = shmat(sharedMemorySegmentId, 0, 0);
        if (sharedMemorySegment == NULL)
        {
            return -1;
        }

        memset(sharedMemorySegment, 0, sizeof(BridgeStat));

        return 0;
    }

    ~ConcurrencyManager()
    {
        if (sharedMemorySegmentId >= 0)
        {
            if (sharedMemorySegment != NULL)
            {
                shmdt(sharedMemorySegment);
            }

            sem_wait(sharedMemoryMutex);
            shmctl(sharedMemorySegmentId, IPC_RMID, NULL);
            sem_post(sharedMemoryMutex);
        }

        if (sharedMemoryMutex != SEM_FAILED)
        {
            sem_unlink(SHARED_MEMORY_MUTEX);
        }

        if (overweightLockGuard != SEM_FAILED)
        {
            sem_unlink(BRIDGE_OVERWEIGHT_LOCK_GUARD);
        }
    }
};

class ScopedLock
{
    sem_t * semaphore;
public:
    ScopedLock(sem_t * _semaphore)
    {
        semaphore = _semaphore;
        sem_wait(semaphore);
    }

    ~ScopedLock()
    {
        sem_post(semaphore);
    }
};

long MaxWeight = 0;

void EnterBridge(const ConcurrencyManager& concurrencyManager, int weight, string vehicle_plate_no)
{   
    bool canEnterBridge = false;

    {
        ScopedLock sharedMemoryMutex(concurrencyManager.sharedMemoryMutex);
        BridgeStat * bridgeStat = (BridgeStat *) (concurrencyManager.sharedMemorySegment);
        canEnterBridge = weight + bridgeStat->currentWeight <= MaxWeight && bridgeStat->waitingVehicles == 0;

        if (canEnterBridge)
        {
            bridgeStat->currentWeight = bridgeStat->currentWeight + weight;
            cout << "Vehicle with Plate #" << vehicle_plate_no << " started crossing the bridge" << endl;
            cout << "Bridge load: " << bridgeStat->currentWeight << endl << endl;
            return;
        }
    }
    
    while (!canEnterBridge)
    {
        {
            ScopedLock sharedMemoryMutex(concurrencyManager.sharedMemoryMutex);
            BridgeStat * bridgeStat = (BridgeStat *)(concurrencyManager.sharedMemorySegment);
            bridgeStat->waitingVehicles = bridgeStat->waitingVehicles + 1;
        }

        sem_wait(concurrencyManager.overweightLockGuard);

        {
            ScopedLock sharedMemoryMutex(concurrencyManager.sharedMemoryMutex);
            BridgeStat * bridgeStat = (BridgeStat *) (concurrencyManager.sharedMemorySegment);
            canEnterBridge = weight + bridgeStat->currentWeight <= MaxWeight;

            if (canEnterBridge)
            {
                bridgeStat->waitingVehicles = bridgeStat->waitingVehicles - 1;
                bridgeStat->currentWeight = bridgeStat->currentWeight + weight;
                cout << "Vehicle with Plate #" << vehicle_plate_no << " started crossing the bridge" << endl;
                cout << "Bridge load: " << bridgeStat->currentWeight << endl << endl;
            }
        }
    }

}

void LeaveBridge(const ConcurrencyManager& concurrencyManager, int weight, string vehicle_plate_no)
{
    ScopedLock sharedMemoryMutex(concurrencyManager.sharedMemoryMutex);
    BridgeStat * bridgeStat = (BridgeStat *)(concurrencyManager.sharedMemorySegment);
    bridgeStat->currentWeight = bridgeStat->currentWeight - weight;

    cout << "Vehicle with Plate #" << vehicle_plate_no << " is leaving the bridge" << endl;
    cout << "Bridge load: " << bridgeStat->currentWeight << endl << endl;

    // Signal if there are waiters
    if (bridgeStat->waitingVehicles > 0)
    {
        sem_post(concurrencyManager.overweightLockGuard);
    }
}

int main(int argc, char *argv [])
{
    if (argc != 2)
    {
        fprintf(stderr, "Usage: %s <max_weight>\n", argv[0]);
        return 1;
    }

    MaxWeight = strtoul(argv[1], NULL, 0);

    cout << "Max Weight: " << MaxWeight << endl << endl;
    ConcurrencyManager concurrencyManager;
    if (concurrencyManager.Initialize() < 0)
    {
        fprintf(stderr, "Synchronization objects initializaton failed: %s\n", strerror(errno));
        return 1;
    }

    string vehicle_plate_no;
    int arrival, weight, bridge_travel_time;
    cin >> vehicle_plate_no >> arrival >> weight >> bridge_travel_time;

    while (cin)
    {
        if (weight > MaxWeight)
        {
            cout << "Vehicle with Plate #" << vehicle_plate_no << " is overweight and rejected from the bridge" << endl << endl;
            cin >> vehicle_plate_no >> arrival >> weight >> bridge_travel_time;
        }
        else
        {
            // Apply delay between arrivals if necessary
            Delay(arrival);
            
            {
                ScopedLock sharedMemoryMutex(concurrencyManager.sharedMemoryMutex);
                BridgeStat * bridgeStat = (BridgeStat *)(concurrencyManager.sharedMemorySegment);
                cout << "Vehicle with Plate #" << vehicle_plate_no << " arrives at the bridge" << endl;
                cout << "Bridge load: " << bridgeStat->currentWeight << endl << endl;
            }

            // Fork a new process for each person arriving in the plaza
            int pid = fork();
            if (pid == 0)
            {
                EnterBridge(concurrencyManager, weight, vehicle_plate_no);

                Delay(bridge_travel_time);

                LeaveBridge(concurrencyManager, weight, vehicle_plate_no);

                return 0;
            }
            else if (pid > 0)
            {
                cin >> vehicle_plate_no >> arrival >> weight >> bridge_travel_time;
            }
            else
            {
                fprintf(stderr, "Failed to fork new process: %s\n", strerror(errno));
                return 1;
            }
        }
    }

    while (waitpid(-1, NULL, 0))
    {
        // The calling process does not have any unwaited-for children
        if (errno == ECHILD)
        {
            break;
        }
    }

    return 0;
}