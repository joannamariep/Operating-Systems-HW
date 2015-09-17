/* Joanna Phillip                                 */
/* CSCI 401: Operating Systems                    */
/* Programming Assignment #1: Process Scheduling  */

#include <assert.h>
#include <iostream>
#include <string>
#include <vector>
#include <algorithm>
#include <unordered_map>
#include <queue>
#include <ctime>

using namespace std;

// Pre-defined keywords/actions to identify process load / start / computational steps
#define NEW         "NEW"
#define START       "START"
#define CPU         "CPU"
#define INPUT       "INPUT"
#define IO          "I/O"

#define MAX_CPU                 4U
#define MAX_IO                  1U
#define MAX_INPUT               1U
#define NO_WAIT_TIME            0U
#define RESOURCE_NOT_IN_USE     0U
#define EMPTY_QUEUE             0U
#define RESOURCE_UNAVAILABLE    -1
#define RESOURCE_NOT_NEEDED     -1

#define SAMPLING_TIME    0.001

// Represent the state of a given process loaded in memory
enum class TimelineState
{
    Invalid = -1,
    Start,    // possibly an exception to the comment above,
              // This is primarily used for parsing / computational purposes
              // to keep track of whether the process has started based on specified input
    CPU_Bound,
    CPU_Ready,
    IO_Bound,
    IO_Wait,
    Input_Bound,
    Input_Wait,
    Terminated
};

// Similar to the TimelineState, but used mainly for the process manager to display information
enum class ProcessState
{
    Invalid = -1,
    Ready,
    Running,
    Waiting,
    Terminated
};

// Converter from TimelineState to ProcessState
ProcessState TimelineStateToProcessState(TimelineState timeline_state)
{
    ProcessState process_state = ProcessState::Invalid;
    switch (timeline_state)
    {
        case TimelineState::CPU_Ready:
        {
            process_state = ProcessState::Ready;
        } break;
        case TimelineState::CPU_Bound:
        {
            process_state = ProcessState::Running;
        } break;
        case TimelineState::Input_Wait:
        case TimelineState::Input_Bound:
        case TimelineState::IO_Wait:
        case TimelineState::IO_Bound:
        {
            process_state = ProcessState::Waiting;
        } break;
        case TimelineState::Terminated:
        {
            process_state = ProcessState::Terminated;
        }
    }

    return process_state;
}

// Converter from TimelineState to the corresponding resource type (string)
string TimelineStateToAssociatedResourceType(TimelineState timeline_state)
{
    string resource = "";
    switch (timeline_state)
    {
        case TimelineState::CPU_Ready:
        case TimelineState::CPU_Bound:
        {
            resource = CPU;
        } break;
        case TimelineState::Input_Wait:
        case TimelineState::Input_Bound:
        {
            resource = INPUT;
        } break;
        case TimelineState::IO_Wait:
        case TimelineState::IO_Bound:
        {
            resource = IO;
        } break;
    }

    return resource;
}

string ProcessStateToString(ProcessState process_state)
{
    string state = "";
    switch (process_state)
    {
        case ProcessState::Invalid:
        {
            state = "INVALID";
        } break;
        case ProcessState::Ready:
        {
            state = "READY";
        } break;
        case ProcessState::Running:
        {
            state = "RUNNING";
        } break;
        case ProcessState::Waiting:
        {
            state = "WAITING";
        } break;
        case ProcessState::Terminated:
        {
            state = "TERMINATED";
        } break;
    }

    return state;
}

// Represents the state of a process at any given time in memory
struct ProcessInfo
{
    ProcessInfo(unsigned int _process_id, unsigned long long _start_time)
    {
        process_id = _process_id;
        start_time = _start_time;

        elapsed_processor_time = 0LL;
        elapsed_input_time = 0LL;
        elapsed_io_time = 0LL;
    }

    unsigned int process_id;
    unsigned long long start_time;
    int cpu_core_used = RESOURCE_NOT_NEEDED;
    int input_resource_used = RESOURCE_NOT_NEEDED;
    int io_resource_used = RESOURCE_NOT_NEEDED;
    unsigned long long elapsed_processor_time;
    unsigned long long elapsed_input_time;
    unsigned long long elapsed_io_time;
    ProcessState state;
};

// Strictly for debugging, tracks outstanding allocations that have not been freed
// If the value is 0, then all outstanding allocations that the variable represents have been freed.
static int procedure_alloc_diff = 0;
static int timeline_entry_alloc_diff = 0;

// The orchestrator of the simulation. Controls the process manager and resource manager, and
// and also provides the data structure that is somewhat mapped to the input file
class TimelineBuilder
{
private:
    // Represents a given unit of workbreakdown for a particular process (e.g. CPU 30ms)
    struct Procedure
    {
        Procedure(TimelineState _state, unsigned long long _duration)
        {
            state = _state;
            duration = _duration;
            next_proc = nullptr;
        }

        Procedure* next_proc;
        TimelineState state;
        unsigned long long duration;
    };

    // Represents an entry for each process
    struct ProcessTimelineEntry
    {
        unsigned int process_id;
        unsigned long long next_update_time;
        unsigned long long total_proc_time;
        Procedure* start_procedure;
    };

    // Manages the resources states, and assign the available cores to
    // requesting resources. If resources are not available, the ResourceManager
    // does not issue any resources [blocks]
    class ResourceManager
    {
        unsigned int m_used_cores;
        unsigned int m_input_requests_in_service;
        unsigned int m_io_requests_in_service;

        bool m_cpu_states[MAX_CPU];
        bool m_input_states[MAX_INPUT];
        bool m_io_states[MAX_IO];

        string ResourceStateToString(bool state)
        {
            if (state)
            {
                return "BUSY";
            }

            return "IDLE";
        }

    public:
        ResourceManager() :
            m_used_cores(0),
            m_input_requests_in_service(0),
            m_io_requests_in_service(0)
        {
            for (int i = 0; i < MAX_CPU; i++)
            {
                m_cpu_states[i] = false;
            }

            for (int i = 0; i < MAX_INPUT; i++)
            {
                m_input_states[i] = false;
            }

            for (int i = 0; i < MAX_IO; i++)
            {
                m_io_states[i] = false;
            }
        }

        int RequestResource(string resource)
        {
            int available_slot = RESOURCE_UNAVAILABLE;

            if (resource == CPU)
            {
                if (m_used_cores == MAX_CPU)
                {
                    return RESOURCE_UNAVAILABLE;
                }

                unsigned int i = RESOURCE_NOT_IN_USE;
                do
                {
                    if (m_cpu_states[i] == false)
                    {
                        available_slot = i;
                        m_cpu_states[i] = true;
                    }

                    i++;
                } while (available_slot == RESOURCE_UNAVAILABLE && i < MAX_CPU);

                ++m_used_cores;
            }
            else if (resource == IO)
            {
                if (m_io_requests_in_service == MAX_IO)
                {
                    return RESOURCE_UNAVAILABLE;
                }

                unsigned int i = RESOURCE_NOT_IN_USE;
                do
                {
                    if (m_io_states[i] == false)
                    {
                        available_slot = i;
                        m_io_states[i] = true;
                    }

                    i++;
                } while (available_slot == RESOURCE_UNAVAILABLE && i < MAX_IO);

                ++m_io_requests_in_service;
            }
            else if (resource == INPUT)
            {
                if (m_input_requests_in_service == MAX_INPUT)
                {
                    return RESOURCE_UNAVAILABLE;
                }

                unsigned int i = RESOURCE_NOT_IN_USE;
                do
                {
                    if (m_input_states[i] == false)
                    {
                        available_slot = i;
                        m_input_states[i] = true;
                    }

                    i++;
                } while (available_slot == RESOURCE_UNAVAILABLE && i < MAX_INPUT);

                ++m_input_requests_in_service;
            }
            else
            {
                // Throw exception to catch implementation bugs
                throw new exception("Unexpected resource type");
            }
            
            assert(available_slot != RESOURCE_UNAVAILABLE && "There should be an available resource to satisfy the request");
            return available_slot;
        }

        // Releases resource if it needs to
        void ReleaseResource(string resource, unsigned int identifier)
        {
            if (resource == CPU)
            {
                if (m_used_cores > RESOURCE_NOT_IN_USE)
                {
                    assert(identifier <= MAX_CPU && L"The integer identifying the resource should be within range of the maximum available resources");

                    if (m_cpu_states[identifier])
                    {
                        m_cpu_states[identifier] = false;
                        --m_used_cores;
                    }
                    // else, no-op
                }
            }
            else if (resource == IO)
            {
                if (m_io_requests_in_service > RESOURCE_NOT_IN_USE)
                {
                    assert(identifier <= MAX_IO && L"The integer identifying the resource should be within range of the maximum available resources");

                    if (m_io_states[identifier])
                    {
                        m_io_states[identifier] = false;
                        --m_io_requests_in_service;
                    }
                    // else, no-op
                }
            }
            else if (resource == INPUT)
            {
                if (m_input_requests_in_service > RESOURCE_NOT_IN_USE)
                {
                    assert(identifier <= MAX_INPUT && L"The integer identifying the resource should be within range of the maximum available resources");

                    if (m_input_states[identifier])
                    {
                        m_input_states[identifier] = false;
                        --m_input_requests_in_service;
                    }
                    // else, no-op
                }
            }
            else
            {
                // Throw exception to catch implementation bugs
                throw new exception("Unexpected resource type");
            }
        }

        bool IsResourceAvailable(string resource)
        {
            bool is_available = false;

            if (resource == CPU)
            {
                is_available = m_used_cores < MAX_CPU;
            }
            else if (resource == IO)
            {
                is_available = m_io_requests_in_service < MAX_IO;

            }
            else if (resource == INPUT)
            {
                is_available = m_input_requests_in_service < MAX_INPUT;
            }
            else
            {
                // Throw exception to catch implementation bugs
                throw new exception("Unexpected resource type");
            }

            return is_available;
        }

        bool IsResourceWithIdentifierAvailable(string resource, unsigned int identifier)
        {
            bool is_available = false;

            if (resource == CPU)
            {
                assert(identifier <= MAX_CPU && L"The integer identifying the resource should be within range of the maximum available resources");
                is_available = m_cpu_states[identifier];
            }
            else if (resource == IO)
            {
                assert(identifier <= MAX_IO && L"The integer identifying the resource should be within range of the maximum available resources");
                is_available = m_io_states[identifier];

            }
            else if (resource == INPUT)
            {
                assert(identifier <= MAX_INPUT && L"The integer identifying the resource should be within range of the maximum available resources");
                is_available = m_input_states[identifier];
            }
            else
            {
                // Throw exception to catch implementation bugs
                throw new exception("Unexpected resource type");
            }

            return is_available;
        }

        void PrintCurrentResourceReport()
        {
            cout << "\t-- STATE OF RESOURCES --" << endl << endl;
            
            cout << "\tCPU\tStatus" << endl;
            
            for (int i = 0; i < MAX_CPU; i++)
            {
                cout << "\t" << i << "\t" << ResourceStateToString(m_cpu_states[i]) << endl;
            }

            cout << endl << "\tI/O\tStatus" << endl;

            for (int i = 0; i < MAX_IO; i++)
            {
                cout << "\t" << i << "\t" << ResourceStateToString(m_io_states[i]) << endl;
            }

            cout << endl << "\tInput\tStatus" << endl;

            for (int i = 0; i < MAX_INPUT; i++)
            {
                cout << "\t" << i << "\t" << ResourceStateToString(m_input_states[i]) << endl;
            }

            cout << endl;
        }
    };

    // Houses the process table and manages the lifetime of each process in memory
    class ProcessManager
    {
        // Represents the process table. The assumption here is that
        // there will be no two processes loaded in memory with the same
        // process id at the same time
        unordered_map<unsigned int, ProcessInfo *> m_process_table;
    public:
        void AddNewProcess(unsigned int process_id, unsigned long long start_time)
        {
            ProcessInfo * new_process_info = new ProcessInfo(process_id, start_time);
            m_process_table.insert(make_pair(process_id, new_process_info));
        }

        void RemoveProcess(unsigned int process_id)
        {
            auto it = m_process_table.find(process_id);
            bool process_exists = it != m_process_table.end();
            assert(process_exists && "Attempting to remove a process that is not in the process table");
            if (process_exists)
            {
                delete it->second;
                m_process_table.erase(it->first);
            }
        }

        // Updates resource usage on every tick
        void IncrementResourceUsageTimeById(string resource, unsigned int process_id)
        {
            auto it = m_process_table.find(process_id);
            bool process_exists = it != m_process_table.end();
            assert(process_exists && "No matching process with specified id");

            if (process_exists)
            {
                ProcessInfo * process_info = it->second;
                
                if (resource == CPU)
                {
                    ++process_info->elapsed_processor_time;
                }
                else if (resource == IO)
                {
                    ++process_info->elapsed_io_time;
                }
                else if (resource == INPUT)
                {
                    ++process_info->elapsed_input_time;
                }
                else
                {
                    // Throw exception to catch implementation bugs
                    throw new exception("Unexpected resource type");
                }
            }
        }

        // Called periodically to ensure that the process state is always up-to-date
        void UpdateProcessState(unsigned int process_id, TimelineState timeline_state, unsigned int resource_identifier)
        {
            auto it = m_process_table.find(process_id);
            bool process_exists = it != m_process_table.end();
            assert(process_exists && "No matching process with specified id");

            if (process_exists)
            {
                ProcessInfo * process_to_update = it->second;
                process_to_update->state = TimelineStateToProcessState(timeline_state);

                // Reset resource use states
                process_to_update->cpu_core_used = RESOURCE_NOT_NEEDED;
                process_to_update->input_resource_used = RESOURCE_NOT_NEEDED;
                process_to_update->io_resource_used = RESOURCE_NOT_NEEDED;
                
                if (timeline_state == TimelineState::CPU_Bound)
                {
                    process_to_update->cpu_core_used = resource_identifier;
                }
                else if (timeline_state == TimelineState::Input_Bound)
                {
                    process_to_update->input_resource_used = resource_identifier;
                }
                else if (timeline_state == TimelineState::IO_Bound)
                {
                    process_to_update->io_resource_used = resource_identifier;
                }
            }
        }

        ProcessInfo * GetProcessInfoById(unsigned int process_id)
        {
            auto it = m_process_table.find(process_id);
            bool process_exists = it != m_process_table.end();

            if (process_exists)
            {
                return it->second;
            }

            return nullptr;
        }

        void PrintCurrentProcessReport()
        {
            cout << "\t-- PROCESSES IN MEMORY --" << endl << endl;
            cout << "\tProcess ID\tStart Time\tProcessor Time\tI/O Time\tInput Time\tCPU Core\tI/O\tInput\tStatus" << endl;
            for each (auto process_info_kvp in m_process_table)
            {
                ProcessInfo * info = process_info_kvp.second;
                cout << "\t" << info->process_id << "\t\t" << info->start_time << "\t\t" << info->elapsed_processor_time << "\t\t" << info->elapsed_io_time
                    << "\t\t" << info->elapsed_input_time << "\t\t"; 
                
                if (info->cpu_core_used == RESOURCE_NOT_NEEDED)
                {
                    cout << "None";
                }
                else
                {
                    cout << info->cpu_core_used;
                }
                
                cout << "\t\t";

                if (info->io_resource_used == RESOURCE_NOT_NEEDED)
                {
                    cout << "None";
                }
                else
                {
                    cout << info->io_resource_used;
                }

                cout << "\t";

                if (info->input_resource_used == RESOURCE_NOT_NEEDED)
                {
                    cout << "None";
                }
                else
                {
                    cout << info->input_resource_used;
                }

                cout << "\t" << ProcessStateToString(info->state) << endl;
            }

            cout << endl;
        }

        ~ProcessManager()
        {
            assert(m_process_table.size() == 0 && "Ideally, there should be no processes in memory while the process manager object is getting destructed");

            // This should not be necessary, but cleanup just in case the object is misused
            for (auto it = m_process_table.begin(); it != m_process_table.end(); ++it)
            {
                delete it->second;
            }

            m_process_table.clear();
        }
    };

    ResourceManager * m_resource_manager;
    ProcessManager * m_process_manager;
    vector<ProcessTimelineEntry *> m_all_proc_timeline;
    list<unsigned int> m_ready_queue;
    list<unsigned int> m_io_queue;
    list<unsigned int> m_input_queue;

    unsigned long long m_total_length_of_timeline = 0;

    // Useful debugging flags
    bool m_initialized = false;

    // Based on the data collected so far, this helper method, returns the amount of time
    // a ready process will have to wait until it can use a CPU core.
    unsigned long long ComputeWaitTimeForResource(string resource, unsigned int excluded_proc_id, unsigned long long currentTime)
    {
        assert(m_all_proc_timeline.size() > 0 && "The process timeline data structure should have been initialized");

        unsigned long long minimum_wait_time = ULLONG_MAX;
        unsigned long long maximum_wait_time_for_existing_resource_waiters = 0U;
        vector<unsigned long long> wait_times;

        if ((resource == CPU && m_resource_manager->IsResourceAvailable(CPU)) ||
            (resource == INPUT && m_resource_manager->IsResourceAvailable(INPUT)) ||
            (resource == IO && m_resource_manager->IsResourceAvailable(IO)))
        {
            return NO_WAIT_TIME;
        }

        for each (ProcessTimelineEntry * entry in m_all_proc_timeline)
        {
            // No need to look at an entry whose process has exited. Also, the calling process
            // is excluded from the computation
            if (currentTime >= entry->total_proc_time || excluded_proc_id == entry->process_id)
            {
                continue;
            }

            unsigned long long elapsed_time = 0U;
            assert(entry->start_procedure && "At this point, the start procedure for an entry should be initialized");
            Procedure * current_procedure = entry->start_procedure;
            elapsed_time += current_procedure->duration;

            while (elapsed_time <= currentTime)
            {
                current_procedure = current_procedure->next_proc;
                assert(current_procedure && "The current procedure should not be null");
                elapsed_time += current_procedure->duration;
            }

            if (resource == CPU)
            {
                if (current_procedure->state == TimelineState::CPU_Bound)
                {
                    wait_times.push_back(elapsed_time - currentTime);
                }
                else if (current_procedure->state == TimelineState::CPU_Ready)
                {
                    // wait time = wait time for existing waiter + resource usage time (after wait) of existing waiter
                    unsigned long long wait_time_left_in_queue = (elapsed_time - currentTime);
                    maximum_wait_time_for_existing_resource_waiters = max(maximum_wait_time_for_existing_resource_waiters, wait_time_left_in_queue);
                    unsigned long long total_wait = wait_time_left_in_queue + current_procedure->next_proc->duration;
                    wait_times.push_back(total_wait);
                }
            }
            else if (resource == INPUT)
            {
                if (current_procedure->state == TimelineState::Input_Bound)
                {
                    wait_times.push_back(elapsed_time - currentTime);
                }
                else if (current_procedure->state == TimelineState::Input_Wait)
                {
                    // wait time = wait time for existing waiter + resource usage time (after wait) of existing waiter
                    unsigned long long wait_time_left_in_queue = (elapsed_time - currentTime);
                    maximum_wait_time_for_existing_resource_waiters = max(maximum_wait_time_for_existing_resource_waiters, wait_time_left_in_queue);
                    unsigned long long total_wait = wait_time_left_in_queue + current_procedure->next_proc->duration;
                    wait_times.push_back(total_wait);
                }
            }
            else if (resource == IO)
            {
                if (current_procedure->state == TimelineState::IO_Bound)
                {
                    wait_times.push_back(elapsed_time - currentTime);
                }
                else if (current_procedure->state == TimelineState::IO_Wait)
                {
                    // wait time = wait time for existing waiter + resource usage time (after wait) of existing waiter
                    unsigned long long wait_time_left_in_queue = (elapsed_time - currentTime);
                    maximum_wait_time_for_existing_resource_waiters = max(maximum_wait_time_for_existing_resource_waiters, wait_time_left_in_queue);
                    unsigned long long total_wait = wait_time_left_in_queue + current_procedure->next_proc->duration;
                    wait_times.push_back(total_wait);
                }
            }
            else
            {
                // Throw exception to catch implementation bugs
                throw new exception("Unexpected resource type");
            }
        }

        for each (unsigned long long wait_time in wait_times)
        {
            if (wait_time > maximum_wait_time_for_existing_resource_waiters)
            {
                minimum_wait_time = min(minimum_wait_time, wait_time);
            }
        }

        return minimum_wait_time;
    }

    TimelineState GetTimelineStateAtTime(ProcessTimelineEntry * entry, unsigned long long time)
    {
        TimelineState timeline_state = TimelineState::Invalid;

        if (entry->start_procedure != nullptr && time <= entry->total_proc_time)
        {
            if (entry->total_proc_time == time)
            {
                timeline_state = TimelineState::Terminated;
            }
            else
            {
                unsigned long long elapsed_time = 0U;
                Procedure * current_procedure = entry->start_procedure;
                elapsed_time += current_procedure->duration;

                while (elapsed_time <= time)
                {
                    current_procedure = current_procedure->next_proc;
                    assert(current_procedure && "The current procedure should not be null");
                    elapsed_time += current_procedure->duration;
                }

                timeline_state = current_procedure->state;
            }
        }

        return timeline_state;
    }

    Procedure * GetProcedureAtTime(ProcessTimelineEntry * entry, unsigned long long time)
    {
        if (entry->start_procedure != nullptr && time <= entry->total_proc_time)
        {
            if (entry->total_proc_time == time)
            {
                return nullptr;
            }
            else
            {
                unsigned long long elapsed_time = 0U;
                Procedure * current_procedure = entry->start_procedure;
                elapsed_time += current_procedure->duration;

                while (elapsed_time <= time)
                {
                    current_procedure = current_procedure->next_proc;
                    assert(current_procedure && "The current procedure should not be null");
                    elapsed_time += current_procedure->duration;
                }

                return current_procedure;
            }
        }

        return nullptr;
    }

    Procedure * GetMostRecentlyCompletedProcedureAtOrBeforeTime(ProcessTimelineEntry * entry, unsigned long long time)
    {
        if (entry->start_procedure != nullptr && time <= entry->total_proc_time)
        {
            unsigned long long elapsed_time = 0U;

            // Use a trailing pointer to follow the procedure before the current procedure at any given time
            Procedure * previous_procedure = entry->start_procedure;
            elapsed_time += previous_procedure->duration;
            Procedure * current_procedure = previous_procedure->next_proc;

            // Sanity check here: By definition, there is no completed procedure before the current procedure
            if (current_procedure == nullptr)
            {
                return nullptr;
            }

            elapsed_time += current_procedure->duration;

            while (elapsed_time <= time)
            {
                previous_procedure = current_procedure;
                current_procedure = current_procedure->next_proc;
                
                // We have reached the end of the linked list
                if (current_procedure == nullptr)
                {
                    break;
                }

                elapsed_time += current_procedure->duration;
            }

            return previous_procedure;
        }

        return nullptr;
    }

    // This call should not be made using a nullptr
    Procedure* TraverseToLastProcedure(Procedure* procedure)
    {
        assert(procedure && "The pointer argument should not be null");
        while (procedure->next_proc != nullptr)
        {
            procedure = procedure->next_proc;
        }

        return procedure;
    }

    void FreeProcessTimelineEntries()
    {
        for each(ProcessTimelineEntry * entry in m_all_proc_timeline)
        {
            FreeProcessTimelineEntry(entry);
        }

        m_all_proc_timeline.clear();
    }

    void FreeProcessTimelineEntry(ProcessTimelineEntry * entry)
    {
        FreeProcedure(entry->start_procedure);
        delete entry;
        --timeline_entry_alloc_diff;
    }

    void FreeProcedure(Procedure * procedure)
    {
        assert(procedure && "The pointer argument should not be null");
        if (procedure->next_proc != nullptr)
        {
            FreeProcedure(procedure->next_proc);
        }
        
        delete procedure;
        --procedure_alloc_diff;
    }
public:
    TimelineBuilder() :
        m_process_manager(nullptr),
        m_resource_manager(nullptr)
    {

    }

    // Clean up resources from heap so as to avoid memory leak
    ~TimelineBuilder()
    {
        // Only need to free up resources if there was any allocated
        if (m_all_proc_timeline.size() != 0)
        {
            FreeProcessTimelineEntries();
        }

        if (m_resource_manager != nullptr)
        {
            delete m_resource_manager;
        }

        if (m_process_manager != nullptr)
        {
            delete m_process_manager;
        }

        assert(procedure_alloc_diff == 0 && "Outstanding procedure allocations");
        assert(timeline_entry_alloc_diff == 0 && "Outstanding timeline entry allocations");
    }

    // During initialization, the assumption here is that a well-formed input will be provided or no input at all. No
    // exception-handling mechanism is implemented for error-recovery with malformed inputs. This function constructs
    // the data structure that is the heart of this application. Other data structures stem from this foundation. The
    // structure essentially places the process entries in sequential order in a linked list, to make it convenient
    // to traverse and make changes as needed.
    bool Initialize()
    {
        assert(!m_initialized && "TimelineBuilder::Initialize() should not be called more than once");
        
        unsigned long long process_elapsed_time = 0;
        
        string keyword;
        unsigned long long value;

        cin >> keyword >> value;
        while (cin)
        {
            int operating_index = m_all_proc_timeline.size() - 1;

            if (keyword == NEW)
            {
                if (operating_index >= 0)
                {
                    m_all_proc_timeline[operating_index]->total_proc_time = process_elapsed_time;
                    m_total_length_of_timeline = max(m_total_length_of_timeline, process_elapsed_time);
                    // reset counter
                    process_elapsed_time = 0;
                }

                ProcessTimelineEntry * entry = new ProcessTimelineEntry();
                ++timeline_entry_alloc_diff;
                entry->process_id = static_cast<unsigned int>(value);
                m_all_proc_timeline.push_back(entry);
            }
            else if (keyword == START)
            {
                ProcessTimelineEntry * entry = m_all_proc_timeline[operating_index];
                entry->start_procedure = new Procedure(TimelineState::Start, value);
                entry->next_update_time = value;
                ++procedure_alloc_diff;
                process_elapsed_time += value;
            }
            else if (keyword == CPU)
            {
                Procedure* lastProcedure = TraverseToLastProcedure(m_all_proc_timeline[operating_index]->start_procedure);
                lastProcedure->next_proc = new Procedure(TimelineState::CPU_Bound, value);
                ++procedure_alloc_diff;
                process_elapsed_time += value;
            }
            else if (keyword == INPUT)
            {
                Procedure* lastProcedure = TraverseToLastProcedure(m_all_proc_timeline[operating_index]->start_procedure);
                lastProcedure->next_proc = new Procedure(TimelineState::Input_Bound, value);
                ++procedure_alloc_diff;
                process_elapsed_time += value;
            }
            else if (keyword == IO)
            {
                Procedure* lastProcedure = TraverseToLastProcedure(m_all_proc_timeline[operating_index]->start_procedure);
                lastProcedure->next_proc = new Procedure(TimelineState::IO_Bound, value);
                ++procedure_alloc_diff;
                process_elapsed_time += value;
            }

            cin >> keyword >> value;
        }
        
        // Remember that this function has been called
        m_initialized = true;

        // If the timeline of processes has not been populated, then there was no valid input
        if (m_all_proc_timeline.size() == 0)
        {
            return false;
        }
        else
        {
            // Update the total process time for the last inserted timeline entry
            int index = m_all_proc_timeline.size() - 1;
            m_all_proc_timeline[index]->total_proc_time = process_elapsed_time;
            m_total_length_of_timeline = max(m_total_length_of_timeline, process_elapsed_time);

            m_resource_manager = new ResourceManager();
            m_process_manager = new ProcessManager();
        }

        return true;
    }

    // This is the entry point to the scheduling procedures that occur on every tick
    void ProcessTimerTick(unsigned long long elapsed_time)
    {
        queue<ProcessTimelineEntry *> delayed_schedulable_process_from_cpu_queue;
        queue<ProcessTimelineEntry *> delayed_schedulable_process_from_io_queue;
        queue<ProcessTimelineEntry *> delayed_schedulable_process_from_input_queue;
        queue<ProcessTimelineEntry *> pending_process_queue;
        queue<ProcessTimelineEntry *> terminated_process_queue;

        for each(ProcessTimelineEntry * entry in m_all_proc_timeline)
        {
            // If the process has not yet been loaded into memory (not started) or has already completed, skip processing
            // for the associated entry
            if (elapsed_time < entry->start_procedure->duration || elapsed_time > entry->total_proc_time)
            {
                continue;
            }

            TimelineState timeline_state = GetTimelineStateAtTime(entry, elapsed_time);

            if (entry->next_update_time == elapsed_time)
            {
                assert(timeline_state != TimelineState::Start && "The next update state cannot be TimelineState::Start because it is always the first state of a process");
                
                Procedure * previous_procedure = GetMostRecentlyCompletedProcedureAtOrBeforeTime(entry, elapsed_time);
                // !HACK! !HACK! !HACK! !HACK! !HACK! !HACK! !HACK! !HACK! !HACK! !HACK! !HACK! !HACK! !HACK! !HACK! 
                // The previous procedure might be an I/O operation with 0 time. To get the true previous procedure,
                // the most convenient way is to decrement the elapsed time by 1 and get the true previous procedure
                // at that time
                bool hijackedPointerDueToZeroIOTime = false;
                if (previous_procedure->state != TimelineState::Start && previous_procedure->duration == 0)
                {
                    previous_procedure = GetProcedureAtTime(entry, elapsed_time - 1);
                    hijackedPointerDueToZeroIOTime = true;
                }

                ReleaseResourceIfAny(previous_procedure, entry->process_id);

                if (timeline_state == TimelineState::CPU_Bound ||
                    timeline_state == TimelineState::Input_Bound ||
                    timeline_state == TimelineState::IO_Bound)
                {
                    string resource = TimelineStateToAssociatedResourceType(timeline_state);

                    if (previous_procedure->state == TimelineState::Start)
                    {
                        m_process_manager->AddNewProcess(entry->process_id, previous_procedure->duration);
                    }
                    
                    assert(resource != "" && "The resource type for the associated timeline state should not be null");

                    if (previous_procedure->state == TimelineState::CPU_Ready)
                    {
                        unsigned int process_id = m_ready_queue.front();
                        assert(process_id == entry->process_id && "The process_id should be match the popped entry's process_id");
                        delayed_schedulable_process_from_cpu_queue.push(entry);
                        m_ready_queue.pop_front();
                        
                        if (hijackedPointerDueToZeroIOTime)
                        {
                            entry->next_update_time += previous_procedure->next_proc->next_proc->duration;
                        }
                        else
                        {
                            entry->next_update_time += previous_procedure->next_proc->duration;
                        }
                    }
                    else if (previous_procedure->state == TimelineState::Input_Wait)
                    {
                        unsigned int process_id = m_input_queue.front();
                        assert(process_id == entry->process_id && "The process_id should be match the popped entry's process_id");
                        delayed_schedulable_process_from_input_queue.push(entry);
                        m_input_queue.pop_front();
                        
                        if (hijackedPointerDueToZeroIOTime)
                        {
                            entry->next_update_time += previous_procedure->next_proc->next_proc->duration;
                        }
                        else
                        {
                            entry->next_update_time += previous_procedure->next_proc->duration;
                        }
                    }
                    else if (previous_procedure->state == TimelineState::IO_Wait)
                    {
                        unsigned int process_id = m_io_queue.front();
                        assert(process_id == entry->process_id && "The process_id should be match the popped entry's process_id");
                        delayed_schedulable_process_from_io_queue.push(entry);
                        m_io_queue.pop_front();
                        
                        if (hijackedPointerDueToZeroIOTime)
                        {
                            entry->next_update_time += previous_procedure->next_proc->next_proc->duration;
                        }
                        else
                        {
                            entry->next_update_time += previous_procedure->next_proc->duration;
                        }
                    }
                    else if ((resource == CPU && delayed_schedulable_process_from_cpu_queue.size() > 0) ||
                            (resource == INPUT && delayed_schedulable_process_from_input_queue.size() > 0) ||
                            (resource == IO && delayed_schedulable_process_from_io_queue.size() > 0))
                    {
                        pending_process_queue.push(entry);
                    }
                    else if ((resource == CPU && m_ready_queue.size() > 0) ||
                            (resource == INPUT && m_input_queue.size() > 0) ||
                            (resource == IO && m_io_queue.size() > 0))
                    {
                        pending_process_queue.push(entry);
                    }
                    else if (m_resource_manager->IsResourceAvailable(resource))
                    {
                        AcquireResource(resource, entry->process_id);
                        
                        if (hijackedPointerDueToZeroIOTime)
                        {
                            entry->next_update_time += previous_procedure->next_proc->next_proc->duration;
                        }
                        else
                        {
                            entry->next_update_time += previous_procedure->next_proc->duration;
                        }
                    }
                    else
                    {
                        bool wait_succeeded = WaitForResource(resource, entry, elapsed_time, previous_procedure);

                        // Wait could fail if there is a chance to reclaim a resource. So retry.
                        if (!wait_succeeded)
                        {
                            pending_process_queue.push(entry);
                        }
                        else
                        {
                            if (hijackedPointerDueToZeroIOTime)
                            {
                                entry->next_update_time += previous_procedure->next_proc->next_proc->duration;
                            }
                            else
                            {
                                entry->next_update_time += previous_procedure->next_proc->duration;
                            }
                        }
                    }
                }
                else if (timeline_state == TimelineState::Terminated)
                {
                    m_process_manager->UpdateProcessState(entry->process_id, TimelineState::Terminated, RESOURCE_NOT_NEEDED);
                    terminated_process_queue.push(entry);
                }
            }
            else
            {
                UpdateResourceUsageTime(timeline_state, entry->process_id);
            }
        }

        ProcessDelayedSchedulableQueue(delayed_schedulable_process_from_cpu_queue, elapsed_time);
        ProcessDelayedSchedulableQueue(delayed_schedulable_process_from_input_queue, elapsed_time);
        ProcessDelayedSchedulableQueue(delayed_schedulable_process_from_io_queue, elapsed_time);
        ProcessPendingQueue(pending_process_queue, elapsed_time);
        ProcessTerminatedQueue(terminated_process_queue, elapsed_time);
    }

    void AcquireResource(string resource, unsigned int process_id)
    {
        unsigned int resource_identifier = m_resource_manager->RequestResource(resource);
        assert(resource_identifier != RESOURCE_UNAVAILABLE && "The resource should be available when TimelineBuilder::AcquireResource() is called");

        TimelineState timeline_state = TimelineState::Invalid;

        if (resource == CPU)
        {
            timeline_state = TimelineState::CPU_Bound;
        }
        else if (resource == INPUT)
        {
            timeline_state = TimelineState::Input_Bound;
        }
        else if (resource == IO)
        {
            timeline_state = TimelineState::IO_Bound;
        }
        else
        {
            // Throw exception to catch implementation bugs
            throw new exception("Unexpected resource type");
        }

        m_process_manager->UpdateProcessState(process_id, timeline_state, resource_identifier);
    }

    void ReleaseResourceIfAny(Procedure * completed_procedure, unsigned int process_id)
    {
        int resource_id = RESOURCE_NOT_NEEDED;

        // The process might have not been added to the process manager when this is call is made, 
        // gracefully return if this is the case
        ProcessInfo * process_info = m_process_manager->GetProcessInfoById(process_id);

        if (process_info == nullptr)
        {
            return;
        }

        TimelineState procedure_state = completed_procedure->state;
        switch (procedure_state)
        {
            case TimelineState::CPU_Bound:
            {
                resource_id = process_info->cpu_core_used;
            } break;
            case TimelineState::Input_Bound:
            {
                resource_id = process_info->input_resource_used;
            } break;
            case TimelineState::IO_Bound:
            {
                resource_id = process_info->io_resource_used;
            }
        }

        // No-op if no resource was found
        if (resource_id != RESOURCE_NOT_NEEDED)
        {
            string resource = TimelineStateToAssociatedResourceType(procedure_state);
            m_resource_manager->ReleaseResource(resource, (unsigned int)resource_id);
            UpdateResourceUsageTime(procedure_state, process_id);
        }
    }

    bool WaitForResource(string resource, ProcessTimelineEntry* entry, unsigned long long current_time, Procedure* most_recently_completed_procedure = nullptr)
    {
        if (most_recently_completed_procedure == nullptr)
        {
            most_recently_completed_procedure = GetMostRecentlyCompletedProcedureAtOrBeforeTime(entry, current_time);
        }

        if (resource == CPU)
        {
            m_ready_queue.push_back(entry->process_id);
        }
        else if (resource == INPUT)
        {
            m_input_queue.push_back(entry->process_id);
        }
        else if (resource == IO)
        {
            m_io_queue.push_back(entry->process_id);
        }
        else
        {
            // Throw exception to catch implementation bugs
            throw new exception("Unexpected resource type");
        }

        unsigned long long wait_time = ComputeWaitTimeForResource(resource, entry->process_id, current_time);
        assert(wait_time != NO_WAIT_TIME && "At this point, the resource should not be available");

        if (wait_time == ULLONG_MAX)
        {
            if (resource == CPU)
            {
                m_ready_queue.pop_back();
            }
            else if (resource == INPUT)
            {
                m_input_queue.pop_back();
            }
            else if (resource == IO)
            {
                m_io_queue.pop_back();
            }
            else
            {
                // Throw exception to catch implementation bugs
                throw new exception("Unexpected resource type");
            }

            return false;
        }

        TimelineState wait_state = TimelineState::Invalid;

        if (resource == CPU)
        {
            wait_state = TimelineState::CPU_Ready;
        }
        else if (resource == INPUT)
        {
            wait_state = TimelineState::Input_Wait;
        }
        else if (resource == IO)
        {
            wait_state = TimelineState::IO_Wait;
        }
        else
        {
            // Throw exception to catch implementation bugs
            throw new exception("Unexpected resource type");
        }

        Procedure * wait_procedure = new Procedure(wait_state, wait_time);
        ++procedure_alloc_diff;
        Procedure * previous_next_procedure = most_recently_completed_procedure->next_proc;
        most_recently_completed_procedure->next_proc = wait_procedure;
        wait_procedure->next_proc = previous_next_procedure;

        entry->total_proc_time += wait_time;
        m_total_length_of_timeline = max(m_total_length_of_timeline, entry->total_proc_time);

        m_process_manager->UpdateProcessState(entry->process_id, wait_state, RESOURCE_UNAVAILABLE);

        return true;
    }

    void ProcessDelayedSchedulableQueue(queue<ProcessTimelineEntry *>& entries, unsigned long long time)
    {
        while (!entries.empty())
        {
            ProcessTimelineEntry * timeline_entry = entries.front();
            TimelineState timeline_state = GetTimelineStateAtTime(timeline_entry, time);
            string resource = TimelineStateToAssociatedResourceType(timeline_state);
            
            int resource_id = m_resource_manager->RequestResource(resource);
            assert(resource_id != RESOURCE_UNAVAILABLE && "The delayed schedulable process should be able to grab a resource");
            m_process_manager->UpdateProcessState(timeline_entry->process_id, timeline_state, resource_id);
            entries.pop();
        }
    }

    void ProcessPendingQueue(queue<ProcessTimelineEntry *>& entries, unsigned long long time)
    {
        while (!entries.empty())
        {
            ProcessTimelineEntry * timeline_entry = entries.front();
            TimelineState timeline_state = GetTimelineStateAtTime(timeline_entry, time);
            string resource = TimelineStateToAssociatedResourceType(timeline_state);

            if (m_resource_manager->IsResourceAvailable(resource))
            {
                int resource_id = m_resource_manager->RequestResource(resource);
                assert(resource_id != RESOURCE_UNAVAILABLE && "The requested resource should be available at this point");
                m_process_manager->UpdateProcessState(timeline_entry->process_id, timeline_state, resource_id);
            }
            else
            {
                WaitForResource(resource, timeline_entry, time);
            }

            Procedure * previous_procedure = GetMostRecentlyCompletedProcedureAtOrBeforeTime(timeline_entry, time);
            timeline_entry->next_update_time += previous_procedure->next_proc->duration;

            entries.pop();
        }
    }

    void ProcessTerminatedQueue(queue<ProcessTimelineEntry *>& entries, unsigned long long time)
    {
        if (!entries.empty())
        {
            PrintSystemReport(time);

            while (!entries.empty())
            {
                ProcessTimelineEntry * timeline_entry = entries.front();
                m_process_manager->RemoveProcess(timeline_entry->process_id);
                entries.pop();
            }
        }
    }

    void PrintResourceQueuesContent()
    {
        cout << "\t-- RESOURCE QUEUES --" << endl << endl;

        cout << "\tCPU Ready Queue" << endl;
        int size_of_queue = m_ready_queue.size();
        cout << "\t";
        int i = 1;
        
        if (size_of_queue == EMPTY_QUEUE)
        {
            cout << "<Empty>";
        }
        else
        {
            for each (auto pid in m_ready_queue)
            {
                cout << "(" << i << ") PID " << pid;
                if (i < size_of_queue)
                {
                    cout << "  <<  ";
                }
                i++;
            }
        }
        
        cout << endl << endl;

        cout << "\tI/O Queue" << endl;
        size_of_queue = m_io_queue.size();
        cout << "\t";
        i = 1;

        if (size_of_queue == EMPTY_QUEUE)
        {
            cout << "<Empty>";
        }
        else
        {
            for each (auto pid in m_io_queue)
            {
                cout << "(" << i << ") PID " << pid;
                if (i < size_of_queue)
                {
                    cout << "  <<  ";
                }
                i++;
            }
        }

        cout << endl << endl;

        cout << "\tInput Queue" << endl;
        size_of_queue = m_input_queue.size();
        cout << "\t";
        i = 1;

        if (size_of_queue == EMPTY_QUEUE)
        {
            cout << "<Empty>";
        }
        else
        {
            for each (auto pid in m_input_queue)
            {
                cout << "(" << i << ") PID " << pid;
                if (i < size_of_queue)
                {
                    cout << "  <<  ";
                }
                i++;
            }
        }

        cout << endl << endl;
    }

    void PrintSystemReport(unsigned long long time)
    {
        cout << "\t*****************************" << endl;
        cout << "\tTime Elapsed: " << time << " ms" << endl;
        cout << "\t*****************************" << endl << endl;

        m_resource_manager->PrintCurrentResourceReport();
        PrintResourceQueuesContent();
        m_process_manager->PrintCurrentProcessReport();
    }

    void UpdateResourceUsageTime(TimelineState timeline_state, unsigned int process_id)
    {
        if (timeline_state == TimelineState::CPU_Bound)
        {
            m_process_manager->IncrementResourceUsageTimeById(CPU, process_id);
        }
        else if (timeline_state == TimelineState::Input_Bound)
        {
            m_process_manager->IncrementResourceUsageTimeById(INPUT, process_id);
        }
        else if (timeline_state == TimelineState::IO_Bound)
        {
            m_process_manager->IncrementResourceUsageTimeById(IO, process_id);
        }
    }

    unsigned long long GetCurrentFullLengthTimeline()
    {
        assert(m_initialized && "TimelineBuilder::GetLengthOfTimeline() should be called after the builder has been initialized");
        return m_total_length_of_timeline;
    }
};

void WaitForNextSample();
int main()
{
    {
        cout << endl;

        TimelineBuilder timelineBuilder;

        // On-demand population (or parsing) of the input provided by the user.
        if (!timelineBuilder.Initialize())
        {
            cout << "No input provided" << endl;
            return -1;
        }

        // Beginning simulation
        bool simulation_complete = false;
        unsigned long long current_simulation_time_in_ms = 0;
        // Poke the timeline builder at every tick so that it can update all states
        timelineBuilder.ProcessTimerTick(current_simulation_time_in_ms);
        unsigned long long total_simulation_time_in_ms = timelineBuilder.GetCurrentFullLengthTimeline();
        while (!simulation_complete)
        {
            WaitForNextSample();
            ++current_simulation_time_in_ms;

            timelineBuilder.ProcessTimerTick(current_simulation_time_in_ms);
            unsigned long long total_simulation_time = timelineBuilder.GetCurrentFullLengthTimeline();
            // Based on the implemented design, the length of the entire simulation is dynamic due to resource contention,
            // so we have to call GetCurrentFullLengthTimeline() after every tick is processed in order to have the latest
            // information
            if (total_simulation_time == current_simulation_time_in_ms)
            {
                simulation_complete = true;
            }
        }
    }

    return 0;
}

void WaitForNextSample()
{
    clock_t before = clock();
    while (true)
    {
        clock_t now = clock();
        double time = (now - before) / (double)CLOCKS_PER_SEC;

        // Break out of loop, if approximately one ms has elapsed;
        if (time >= SAMPLING_TIME)
        {
            break;
        }
    }
}