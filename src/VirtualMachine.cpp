#include <stdlib.h>
#include <functional>
#include <queue>
#include <vector>
#include <stack>
#include <map>
#include <cstring>
#include <string>
#include <fcntl.h>
#include "VirtualMachine.h"
#include "Machine.h"
#include <iostream>


extern "C"
{
  TVMMainEntry VMLoadModule(const char *module);
  void VMUnloadModule();

  TVMStatus OG_VMFileOpen(TMachineSignalStateRef mask, const char *filename, int flags, int mode, int *filedescriptor);
  TVMStatus OG_VMFileClose(TMachineSignalStateRef mask, int filedescriptor);
  TVMStatus OG_VMFileRead(TMachineSignalStateRef mask, int filedescriptor, void *data, int *length);
  TVMStatus OG_VMFileWrite(TMachineSignalStateRef mask, int filedescriptor, void *data, int *length);
  TVMStatus OG_VMFileSeek(TMachineSignalStateRef mask, int filedescriptor, int offset, int whence, int *newoffset);

  struct SkeletonArgs
  {
    TVMThreadEntry fn;
    void *args_ptr;
  };

  void Skeleton(void* args);

  class TCB
  {
    inline static unsigned int thread_count = 1;
    TVMThreadID thread_id;

    void* stack_ptr = NULL;
    TVMMemorySize stack_size = static_cast<TVMMemorySize>(0);

    TVMThreadEntry fn_ptr = NULL;
    void *args_ptr = NULL;
    SkeletonArgs entry_fn_args;

  public:
    const static unsigned int INVALID_ID = 0;

    TVMThreadState thread_state = VM_THREAD_STATE_DEAD;
    TVMThreadPriority priority = VM_THREAD_PRIORITY_LOW;

    SMachineContext context;
    TVMTick last_scheduled = 0;
    TVMTick enable_time = 0;

    TVMThreadID get_thread_id() { return thread_id; }

    TCB(TVMThreadPriority prio): priority(prio) {
      thread_id = TCB::thread_count;
      TCB::thread_count += 1;
    }

    TCB(TVMThreadEntry entry_fn, void *entry_args,
      TVMMemorySize memsize, TVMThreadPriority prio) :
      stack_size(memsize), fn_ptr(entry_fn),
      args_ptr(entry_args), priority(prio) {
        thread_id = TCB::thread_count;
        TCB::thread_count += 1;
      }

    ~TCB()
    {
      if (stack_ptr != NULL)
      {
        free(stack_ptr);
      }
    }

    void initialize_context()
    {
      stack_ptr = malloc(stack_size);

      entry_fn_args = {fn_ptr, args_ptr};
      void* cast_args = static_cast<void*>(&entry_fn_args);

      MachineContextCreate(
        &context, Skeleton, cast_args, stack_ptr, stack_size);
    }
  };

  TCB *IDLE_THREAD_PTR = NULL;

  // used internally for comparison of priorities.
  // Because of this functiton we're independent from actual values
  // assigned to priority constants
  int priority_to_int(const TCB* ptr)
  {
    auto p = ptr -> priority;
    if (p == VM_THREAD_PRIORITY_LOW)
      return 1;
    if (p == VM_THREAD_PRIORITY_NORMAL)
      return 2;
    return 3; // if priority = high
  }


  struct TcbPriorityCompare
  {
    bool operator()(const TCB* one_ptr, const TCB* two_ptr) const
    {
      int p1 = priority_to_int(one_ptr);
      int p2 = priority_to_int(two_ptr);

      // if two threads have different priorities, prioritize one with
      // higher priority
      if (p1 != p2)
        return p1 < p2;

      // prioritize down the idle thread
      if (one_ptr == IDLE_THREAD_PTR)
      {
        return true;
      }
      else if (two_ptr == IDLE_THREAD_PTR)
      {
        return false;
      }
      // if two threads have same priority, but different schedule times
      // choose one that was scheduled earlier
      // push idle thread to the back though
      else
      {
        return one_ptr -> last_scheduled > two_ptr -> last_scheduled;
      }
    }
  } tcb_ready_compare;

  struct TcbWaitCompare
  {
    bool operator()(const TCB* one_ptr, const TCB* two_ptr) const
    {
      return (one_ptr -> enable_time > two_ptr -> enable_time);
    }
  };

  class Lock
  {
    bool is_acquired = false;
    TVMMutexID lock_id;
    TCB* thread_holder = NULL;

    std::priority_queue<TCB*, std::vector<TCB*>, TcbPriorityCompare > wait_list;
  public:
    inline static unsigned int lock_count = 5;
    Lock();
    bool acquire(TVMTick timeout, TMachineSignalState* mask);
    void release();
    TVMMutexID get_id();
    bool is_free();
    TCB* get_owner_thread();
  };


  class MemoryManager
  {
    TVMMutexID mem_alloc_lock_id;

    int total_mem_size;
    char *shared_mem_ptr = NULL;

    int cell_num;
    std::vector<int> free_cells;

    std::priority_queue<TCB*, std::vector<TCB*>, TcbPriorityCompare > wait_list;

  public:
    inline static const unsigned int CHUNK_SIZE = 512; // small mem?

    MemoryManager(int sz, void *mem_ptr);

    void allocate_cell(int *id, char **address, TMachineSignalState** mask);
    void free_memory_cell(int id);
  };


  MemoryManager::MemoryManager(int sz, void *mem_ptr): \
    total_mem_size(sz), shared_mem_ptr(static_cast<char*>(mem_ptr))
  {
    VMMutexCreate(&mem_alloc_lock_id);
    cell_num = sz / MemoryManager::CHUNK_SIZE;
    for (int i = 0; i < cell_num; i++) {
      free_cells.push_back(i);
    }
  };

  // @TODO: MANAGE BACKUP
  class FAT
  {
    int mount_fd = -1;
    const char *mount_name = NULL;

    unsigned long bytes_per_sector; // BPB_BytsPerSec
    unsigned long sectors_per_cluster; // BPB_SecPerClus
    unsigned long fats_count; // BPB_NumFATs
    unsigned long root_entries_count; // BPB_RootEntCnt
    unsigned long total_sectors_count; // BPB_TotSec16 or BPB_TotSec32
    unsigned long media_value; // BPB_Media
    unsigned long eof_value; // 0xff <media_value>
    unsigned long sectors_per_fat; // BPB_FATSz16
    unsigned long volume_serial_number; // BS_VolID
    unsigned long reserved_sectors_count; // BPB_RsvdSecCnt
    unsigned long all_fats_sectors_count;
    unsigned long root_dir_sectors_count;
    unsigned long fat_offset;
    unsigned long root_dir_offset;

    char volume_label[12]; // @TODO: unsigned char? 11?

    std::vector<std::string> path;
    std::vector<unsigned int> fat;

    static unsigned long little_endian(void *ptr, int count);

  public:
    bool init(const char *mount_name);
    void current_working_directory(char *abs_path);
  };

  unsigned long FAT::little_endian(void *ptr, int count)
  {
    unsigned char* ch_ptr = (unsigned char*) ptr;
    unsigned long res = 0;
    for (int i = count - 1; i >= 0; i--)
    {
      res = (res << 8) + *(ch_ptr + i);
    }

    return res;
  }

  void print_root_dir_line(char* str)
  {
    char ATTR_READ_ONLY = 0x01;
    char ATTR_HIDDEN = 0x02;
    char ATTR_SYSTEM = 0x04;
    char ATTR_VOLUME_ID = 0x08;
    char ATTR_DIRECTORY = 0x10;
    char ATTR_ARCHIVE = 0x20;
    char ATTR_LONG_NAME = ATTR_READ_ONLY | ATTR_HIDDEN | ATTR_SYSTEM | ATTR_VOLUME_ID;


    char short_name[12];
    short_name[11] = '\0';


    if (((int) str[11]) == long_fname)
    {
      return;
    }

    // printing name
    std::cout << "[SHORT NAME] ";
    for (int i = 0; i < 11; i++)
    {
      std::cout << str[i];
    }
    std::cout << "\n";

    // attr
    std::cout << "[Attr] 0x" << std::hex << int(str[11]) << "\n";
    std::cout << "[NTR] 0x" << std::hex << int(str[12]) << "\n";
  }

  bool FAT::init(const char *mount_name)
  {
    TMachineSignalState mask;
    MachineSuspendSignals(&mask);
    OG_VMFileOpen(&mask, mount_name, O_RDWR, 600, &mount_fd);
    MachineSuspendSignals(&mask);

    if (mount_fd < 0)
    {
      MachineResumeSignals(&mask);
      return false;
    }

    volatile int data_len = 512;
    volatile unsigned char data[data_len];

    void *data_ptr = (void*) &data;
    int *data_len_ptr = (int*) &data_len;

    TVMStatus res = OG_VMFileRead(&mask, mount_fd, data_ptr, data_len_ptr);
    MachineSuspendSignals(&mask);

    if (res != VM_STATUS_SUCCESS)
    {
      MachineResumeSignals(&mask);
      return false;
    }

    // assert is FAT
    if ((data[510] != 0x55) || (data[511] != 0xAA))
    {
      VMPrintError(
        "Improperly formated FAT volume. \
        According to FAT specs, values of volume[510] and volume[511] must \
        be 0x55 and 0xAA. Mismatch. Not a FAT volume provided\n");
      MachineResumeSignals(&mask);
      return false;
    }

    bytes_per_sector = FAT::little_endian(data_ptr + 11, 2);
    sectors_per_cluster = data[13];
    reserved_sectors_count = FAT::little_endian(data_ptr + 14, 2);
    fats_count = data[16];
    root_entries_count = FAT::little_endian(data_ptr + 17, 2);

    unsigned long total_sec_16 = FAT::little_endian(data_ptr + 19, 2);
    unsigned long total_sec_32 = FAT::little_endian(data_ptr + 32, 4);
    if ((total_sec_16 == 0) && (total_sec_32 == 0))
    {
      VMPrintError(
        "Improperly formated FAT volume.\
        Both BPB_TotSec32 or BPB_TotSec16 are 0. \
        Exactly one must have positive value.\n");
      MachineResumeSignals(&mask);
      return false;
    }
    else if ((total_sec_16 != 0) && (total_sec_32 != 0))
    {
      VMPrintError(
        "Improperly formated FAT volume. \
        Both BPB_TotSec32 or BPB_TotSec16 are not 0. \
        Exactly one must have positive value.\n");
      MachineResumeSignals(&mask);
      return false;
    }
    else if (total_sec_16 != 0)
    {
      total_sectors_count = total_sec_16;
    }
    else
    {
      total_sectors_count = total_sec_32;
    }

    media_value = data[21];
    eof_value = (0xff << 8) + media_value;
    sectors_per_fat = FAT::little_endian(data_ptr + 22, 2);
    volume_serial_number = FAT::little_endian(data_ptr + 39, 4);
    for (int i = 0; i < 11; i++)
    {
      volume_label[i] = data[43 + i];
    }
    volume_label[11] = '\0';


    // reserved_sectors_count
    all_fats_sectors_count = sectors_per_fat * fats_count;
    root_dir_sectors_count =
      ((root_entries_count * 32) + (bytes_per_sector - 1)) / bytes_per_sector;

    fat_offset = reserved_sectors_count * bytes_per_sector;
    root_dir_offset =
      (reserved_sectors_count + all_fats_sectors_count) * bytes_per_sector;

    std::cout << ">> bytes_per_sector = " << bytes_per_sector << "\n";
    std::cout << ">> sectors_per_cluster = " << sectors_per_cluster << "\n";
    std::cout << ">> fats_count = " << fats_count << "\n";
    std::cout << ">> root_entries_count = " << root_entries_count << "\n";
    std::cout << ">> media_value = " << media_value << "\n";
    std::cout << ">> sectors_per_fat = " << sectors_per_fat << "\n";
    std::cout << ">> volume_serial_number = " << volume_serial_number << "\n";
    std::cout << ">> volume_label = " << volume_label << "\n";
    std::cout << ">> reserved_sectors_count = " << reserved_sectors_count << "\n";
    std::cout << ">> all_fats_sectors_count = " << all_fats_sectors_count << "\n";
    std::cout << ">> root_dir_sectors_count = " << root_dir_sectors_count << "\n";
    std::cout << ">> fat_offset = " << fat_offset << "\n";
    std::cout << ">> root_dir_offset = " << root_dir_offset << "\n";
    std::cout << ">> eof_value = " << eof_value << "\n";


    // read FAT table
    // increase capacity:
    // - num of sectors in 1 fat * bytes per sector / bytes per entry
    int fat_size = sectors_per_fat * bytes_per_sector / 2;
    fat.reserve(fat_size);

    // go to the beginning of fat
    int tmp = -1;
    res = OG_VMFileSeek(&mask, mount_fd, fat_offset, 0, &tmp);
    MachineSuspendSignals(&mask);
    if (res != VM_STATUS_SUCCESS)
    {
      MachineResumeSignals(&mask);
      return false;
    }

    volatile int fat_data_len = sectors_per_fat * bytes_per_sector;
    volatile unsigned char fat_data[fat_data_len];
    void *fat_data_ptr = (void*) &fat_data;
    int *fat_data_len_ptr = (int*) &fat_data_len;

    res = OG_VMFileRead(&mask, mount_fd, fat_data_ptr, fat_data_len_ptr);
    MachineSuspendSignals(&mask);

    if (res != VM_STATUS_SUCCESS)
    {
      MachineResumeSignals(&mask);
      return false;
    }
    for (int i = 0; i < fat_size; i++)
    {
      fat[i] = FAT::little_endian(fat_data_ptr + 2 * i, 2);
    }
    // assert 0xfff8 0xffff
    if ((fat[0] != eof_value) || (fat[1] != 65535))
    {
      VMPrintError(
        "Improperly formated FAT volume. \
        First values in fat table are expected to be 0xff<BPB_Media> and 0xffff\n");
      MachineResumeSignals(&mask);
      return false;
    }

    // READ ROOT DIR
    // FIGURE OUT CORRELATION BETWEEN ROOT DIR and FAT





    // go to the beginning of root dir
    tmp = -1;
    res = OG_VMFileSeek(&mask, mount_fd, root_dir_offset, 0, &tmp);
    MachineSuspendSignals(&mask);
    if (res != VM_STATUS_SUCCESS)
    {
      MachineResumeSignals(&mask);
      return false;
    }

    volatile int root_dir_data_len = root_dir_sectors_count * bytes_per_sector;
    volatile unsigned char root_dir_data[root_dir_data_len];
    void *root_dir_data_ptr = (void*) &root_dir_data;
    int *root_dir_data_len_ptr = (int*) &root_dir_data_len;

    res = OG_VMFileRead(&mask, mount_fd, root_dir_data_ptr, root_dir_data_len_ptr);
    MachineSuspendSignals(&mask);

    if (res != VM_STATUS_SUCCESS)
    {
      MachineResumeSignals(&mask);
      return false;
    }

    int i = 0;
    while (true)
    {
      if ((root_dir_data[i] == 0x00) || (root_dir_data[i] == 0xE5))
      {
        break;
      }
      char tmp_str[32];

      for (int j = 0; j < 32; j++)
      {
        tmp_str[j] = root_dir_data[i + j];
      }

      print_root_dir_line(tmp_str);
      i += 32;

    }

    MachineResumeSignals(&mask);
    return true;
  }

  // @TODO: handle long paths; to test
  void FAT::current_working_directory(char *abs_path)
  {
    abs_path[0] = '/';
    int size = path.size();
    int char_i = 1;

    if (size > 0)
    {
      // iterate over each dir
      for (int path_i = 0; path_i < size; path_i++)
      {
        // iterate over each char in the dir
        for (int j = 0; j < path[path_i].length(); j++, char_i++)
        {
          abs_path[char_i] = path[path_i][j];
        }
      }
    }
    abs_path[char_i] = '\0';
  }

  class GlobalState
  {
  public:
    std::priority_queue<TCB*, std::vector<TCB*>, TcbPriorityCompare > ready_list;
    std::priority_queue<TCB*, std::vector<TCB*>, TcbWaitCompare > wait_list;

    std::map<TVMThreadID, TCB* > all_threads;
    std::map<TVMMutexID, Lock*> all_locks;
    TCB *cur_thread_ptr = NULL;
    MemoryManager *mem_mgr_ptr = NULL;

    int tick_ms = 0;
    TVMTick current_time = 0;
    FAT fat;

    ~GlobalState()
    {
      if (mem_mgr_ptr != NULL)
      {
        delete mem_mgr_ptr;
      }
    }
  } global_state;

  void Skeleton(void* args)
  {
    SkeletonArgs* fn_with_args = static_cast<SkeletonArgs*>(args);
    fn_with_args -> fn(fn_with_args -> args_ptr);
    VMThreadTerminate(global_state.cur_thread_ptr -> get_thread_id());
  };

  void InfiniteLoop(void* args)
  {
    while(true);
  };

  void create_idle_thread()
  {
    TVMThreadID idle_thread;
    VMThreadCreate(InfiniteLoop, NULL, 0x100000, VM_THREAD_PRIORITY_LOW, &idle_thread);
    VMThreadActivate(idle_thread);
    IDLE_THREAD_PTR = global_state.all_threads[idle_thread];
  }

  void create_main_thread()
  {
    TCB *tcb_ptr = new TCB(VM_THREAD_PRIORITY_NORMAL);
    tcb_ptr -> thread_state = VM_THREAD_STATE_RUNNING;
    global_state.all_threads[tcb_ptr -> get_thread_id()] = tcb_ptr;
    global_state.cur_thread_ptr = tcb_ptr;
  }

  void schedule_threads(TMachineSignalStateRef sig_state_ptr)
  {
    // @WARN: MAKE SURE TO SUSPEND SIGNALS BEFORE ENTERING THIS FUNCTION

    // Processing Wait list
    while (global_state.wait_list.size() != 0)
    {
      TCB* wait_tcb_ptr = global_state.wait_list.top();

      if (wait_tcb_ptr -> enable_time <= global_state.current_time)
      {
        global_state.wait_list.pop();
        wait_tcb_ptr -> enable_time = 0;
        wait_tcb_ptr -> thread_state = VM_THREAD_STATE_READY;
        global_state.ready_list.push(wait_tcb_ptr);
      }
      else
      {
        break;
      }
    }

    // If it's the only thread running, do nothing
    if (global_state.ready_list.size() == 0)
    {
      MachineResumeSignals(sig_state_ptr);
      return;
    }

    // FROM DOCUMENTATION:
    // >>> If tick is specified as VM_TIMEOUT_IMMEDIATE the current process
    // >>> yields the remainder of its processing quantum to the next ready
    // >>> process of equal priority.

    // At this point, 3 scenarios are possible:
    // Case 1 [Switch]: Cur thread is in RUNNING state and in no list
    // Case 2 [Switch]: Cur thread is in WAITING State and in wait_list
    // Case 3: Cur thread is in READY state an in ready_list:
    //         the thread yielded to another thread of the same priority
    //
    // Case 3.1 [Switch]: There's another thread of equal priority =>
    //         execute new thread
    // Case 3.2 [Don't switch]: There's no other thread of equal priority =>
    //         continue cur thread
    //
    // [Per documentation: need to yield to the next thread of equal priority.
    // Thus, if there's no other => keep the same thread]

    TCB* cur_th_ptr = global_state.cur_thread_ptr;

    bool is_cur_th_dead = cur_th_ptr -> thread_state == VM_THREAD_STATE_DEAD;
    bool is_cur_th_run = cur_th_ptr -> thread_state == VM_THREAD_STATE_RUNNING;
    bool is_cur_th_wait = cur_th_ptr -> thread_state == VM_THREAD_STATE_WAITING;
    bool is_cur_th_ready = cur_th_ptr -> thread_state == VM_THREAD_STATE_READY;

    if (is_cur_th_run)
    {
      global_state.ready_list.push(cur_th_ptr);
    }

    TCB* next_th_ptr = global_state.ready_list.top();
    global_state.ready_list.pop();

    // highest priority will always be at the top of the queue
    bool is_cur_th_ready_swtch = is_cur_th_ready && (cur_th_ptr != next_th_ptr);
    bool is_cur_th_run_swtch = is_cur_th_run && (cur_th_ptr != next_th_ptr);

    bool should_switch = is_cur_th_dead || is_cur_th_wait ||
       is_cur_th_run_swtch || is_cur_th_ready_swtch;

    if (should_switch)
    {
      next_th_ptr -> thread_state = VM_THREAD_STATE_RUNNING;
      next_th_ptr -> last_scheduled = global_state.current_time;
      global_state.cur_thread_ptr = next_th_ptr;
    }

    if (is_cur_th_run_swtch)
    {
      cur_th_ptr -> thread_state = VM_THREAD_STATE_READY;
    }

    if (should_switch)
    {
      MachineResumeSignals(sig_state_ptr);
      MachineContextSwitch(&(cur_th_ptr -> context), &(next_th_ptr -> context));
    }
    else
    {
      cur_th_ptr -> thread_state = VM_THREAD_STATE_RUNNING;

      int ch = RAND_MAX / 4;
      int r = std::rand();

      if ((IDLE_THREAD_PTR != NULL) && (cur_th_ptr != IDLE_THREAD_PTR) && (r <= ch)
        && (IDLE_THREAD_PTR -> thread_state == VM_THREAD_STATE_READY))
      {

        std::stack<TCB*> tmp_stack;
        TCB* tmp_tcb_ptr = NULL;
        bool removed = false;

        while(global_state.ready_list.size() != 0)
        {
          tmp_tcb_ptr = global_state.ready_list.top();
          global_state.ready_list.pop();
          if (tmp_tcb_ptr != IDLE_THREAD_PTR)
          {
            tmp_stack.push(tmp_tcb_ptr);
          }
          else
          {
            removed = true;
          }
        }

        while (tmp_stack.size() != 0)
        {
          tmp_tcb_ptr = tmp_stack.top();
          tmp_stack.pop();
          global_state.ready_list.push(tmp_tcb_ptr);
        }

        if (removed)
        {
          cur_th_ptr -> thread_state = VM_THREAD_STATE_READY;
          cur_th_ptr -> enable_time = 0;
          global_state.ready_list.push(cur_th_ptr);
          IDLE_THREAD_PTR -> thread_state = VM_THREAD_STATE_RUNNING;
          IDLE_THREAD_PTR -> last_scheduled = global_state.current_time;
          global_state.cur_thread_ptr = IDLE_THREAD_PTR;
          MachineResumeSignals(sig_state_ptr);
          MachineContextSwitch(&(cur_th_ptr -> context), &(IDLE_THREAD_PTR -> context));
        }
        else
        {
          MachineResumeSignals(sig_state_ptr);
        }
      }
      else
      {
        MachineResumeSignals(sig_state_ptr);
      }
    }
  }

  void InterruptCB(void* args)
  {
    TMachineSignalState signal_mask_state;
    MachineSuspendSignals(&signal_mask_state);
    // @TODO<Low Priority>: potential overflow
    // with ticks every ms it'd take about (2^32)/1000/60/60/24 ~= 50 days
    // to overflow. Won't fix for this assignment
    global_state.current_time += 1;

    schedule_threads(&signal_mask_state);
  }

  void MemoryManager::allocate_cell(int *id, char **address, TMachineSignalState** mask)
  {
    VMMutexAcquire(mem_alloc_lock_id, VM_TIMEOUT_INFINITE);

    if (free_cells.size() == 0)
    {

      TCB* cur_th_ptr = global_state.cur_thread_ptr;
      cur_th_ptr -> thread_state = VM_THREAD_STATE_WAITING;

      wait_list.push(cur_th_ptr);

      VMMutexRelease(mem_alloc_lock_id);
      schedule_threads(*mask);
      MachineSuspendSignals(*mask);
      VMMutexAcquire(mem_alloc_lock_id, VM_TIMEOUT_INFINITE);
    }

    *id = free_cells.back();
    free_cells.pop_back();

    *address = shared_mem_ptr + (*id) * MemoryManager::CHUNK_SIZE;

    for (unsigned int i = 0; i < MemoryManager::CHUNK_SIZE; i++) // remove??
    {
      *(*address + i) = '\0';
    }

    VMMutexRelease(mem_alloc_lock_id);
  }

  void MemoryManager::free_memory_cell(int id)
  {
    VMMutexAcquire(mem_alloc_lock_id, VM_TIMEOUT_INFINITE); // possibly can remove

    free_cells.push_back(id);
    if (wait_list.size() != 0)
    {
      TCB* next_th_ptr = wait_list.top();
      wait_list.pop();
      next_th_ptr -> thread_state = VM_THREAD_STATE_READY;
      next_th_ptr -> enable_time = 0;
      global_state.ready_list.push(next_th_ptr);
    }

    VMMutexRelease(mem_alloc_lock_id);
  }

  // +
  TVMStatus VMStart(int tickms, TVMMemorySize sharedsize, const char *mount, int argc, char *argv[])
  {
    TVMMainEntry fnEntry = VMLoadModule(argv[0]);
    if ((fnEntry == NULL) || (mount == NULL))
    {
      return VM_STATUS_FAILURE;
    }

    global_state.tick_ms = tickms;

    TVMMemorySize actual_size = (sharedsize / 4096 );
    actual_size *= 4096;
    if (actual_size < sharedsize)
    {
      actual_size += 4096;
    }

    global_state.mem_mgr_ptr = \
      new MemoryManager(actual_size, MachineInitialize(actual_size));

    MachineEnableSignals();
    MachineRequestAlarm(global_state.tick_ms * 1000, InterruptCB, NULL);

    create_main_thread();
    create_idle_thread();

    if (!global_state.fat.init(mount))
    {
      return VM_STATUS_FAILURE;
    }

    fnEntry(argc, argv);

    MachineTerminate();
    VMUnloadModule();

    return VM_STATUS_SUCCESS;
  }

  TVMStatus VMTickMS(int *tickmsref)
  {
    TMachineSignalState signal_mask_state;
    MachineSuspendSignals(&signal_mask_state);

    if (tickmsref == NULL)
    {
      MachineResumeSignals(&signal_mask_state);
      return VM_STATUS_ERROR_INVALID_PARAMETER;
    }

    *tickmsref = global_state.tick_ms;

    MachineResumeSignals(&signal_mask_state);
    return VM_STATUS_SUCCESS;
  }

  TVMStatus VMTickCount(TVMTickRef tickref)
  {
    TMachineSignalState signal_mask_state;
    MachineSuspendSignals(&signal_mask_state);

    if (tickref == NULL)
    {
      MachineResumeSignals(&signal_mask_state);
      return VM_STATUS_ERROR_INVALID_PARAMETER;
    }

    *tickref = global_state.current_time;

    MachineResumeSignals(&signal_mask_state);
    return VM_STATUS_SUCCESS;
  }

  TVMStatus VMThreadCreate(
    TVMThreadEntry entry, void *param, TVMMemorySize memsize,
    TVMThreadPriority prio, TVMThreadIDRef tid)
  {
    TMachineSignalState signal_mask_state;
    MachineSuspendSignals(&signal_mask_state);

    if (entry == NULL || tid == NULL)
    {
      MachineResumeSignals(&signal_mask_state);
      return VM_STATUS_ERROR_INVALID_PARAMETER;
    }

    TCB *tcb_ptr = new TCB(entry, param, memsize, prio);
    *tid = tcb_ptr -> get_thread_id();
    global_state.all_threads[tcb_ptr -> get_thread_id()] = tcb_ptr;

    MachineResumeSignals(&signal_mask_state);
    return VM_STATUS_SUCCESS;
  }

  TVMStatus VMThreadDelete(TVMThreadID thread)
  {
    TMachineSignalState signal_mask_state;
    MachineSuspendSignals(&signal_mask_state);

    TCB* tcb_ptr = global_state.all_threads[thread];
    if (tcb_ptr == NULL)
    {
      MachineResumeSignals(&signal_mask_state);
      return VM_STATUS_ERROR_INVALID_ID;
    }

    if (tcb_ptr -> thread_state != VM_THREAD_STATE_DEAD)
    {
      MachineResumeSignals(&signal_mask_state);
      return VM_STATUS_ERROR_INVALID_STATE;
    }

    global_state.all_threads.erase(thread);
    delete tcb_ptr;

    MachineResumeSignals(&signal_mask_state);
    return VM_STATUS_SUCCESS;
  }

  TVMStatus VMThreadActivate(TVMThreadID thread)
  {
    TMachineSignalState signal_mask_state;
    MachineSuspendSignals(&signal_mask_state);

    TCB *tcb_ptr = global_state.all_threads[thread];
    if (tcb_ptr == NULL)
    {
      MachineResumeSignals(&signal_mask_state);
      return VM_STATUS_ERROR_INVALID_ID;
    }

    if (tcb_ptr -> thread_state != VM_THREAD_STATE_DEAD)
    {
      MachineResumeSignals(&signal_mask_state);
      return VM_STATUS_ERROR_INVALID_STATE;
    }

    tcb_ptr -> initialize_context();
    tcb_ptr -> thread_state = VM_THREAD_STATE_READY;
    global_state.ready_list.push(tcb_ptr);
    schedule_threads(&signal_mask_state);

    return VM_STATUS_SUCCESS;
  }

  TVMStatus VMThreadTerminate(TVMThreadID thread)
  {
    TMachineSignalState signal_mask_state;
    MachineSuspendSignals(&signal_mask_state);

    TCB *tcb_ptr = global_state.all_threads[thread];
    if (tcb_ptr == NULL)
    {
      MachineResumeSignals(&signal_mask_state);
      return VM_STATUS_ERROR_INVALID_ID;
    }

    if (tcb_ptr -> thread_state == VM_THREAD_STATE_DEAD)
    {
      MachineResumeSignals(&signal_mask_state);
      return VM_STATUS_ERROR_INVALID_STATE;
    }

    std::stack<TCB*> tmp_stack;
    TCB* tmp_tcb_ptr = NULL;

    if (tcb_ptr -> thread_state == VM_THREAD_STATE_READY)
    {
      while (global_state.ready_list.size() != 0)
      {
        tmp_tcb_ptr = global_state.ready_list.top();
        global_state.ready_list.pop();
        if (tmp_tcb_ptr != tcb_ptr)
        {
          tmp_stack.push(tmp_tcb_ptr);
        }
      }

      while (tmp_stack.size() != 0)
      {
        tmp_tcb_ptr = tmp_stack.top();
        tmp_stack.pop();
        global_state.ready_list.push(tmp_tcb_ptr);
      }
    }

    if (tcb_ptr -> thread_state == VM_THREAD_STATE_WAITING)
    {
      while (global_state.wait_list.size() != 0)
      {
        tmp_tcb_ptr = global_state.wait_list.top();
        global_state.wait_list.pop();
        if (tmp_tcb_ptr != tcb_ptr)
        {
          tmp_stack.push(tmp_tcb_ptr);
        }
      }

      while (tmp_stack.size() != 0)
      {
        tmp_tcb_ptr = tmp_stack.top();
        tmp_stack.pop();
        global_state.wait_list.push(tmp_tcb_ptr);
      }
    }

    tcb_ptr -> thread_state = VM_THREAD_STATE_DEAD;
    for (unsigned int i = 0; i < Lock::lock_count; i++)
    {
      Lock *lk_ptr = global_state.all_locks[i];
      if (lk_ptr == NULL)
      {
        continue;
      }

      if (lk_ptr -> get_owner_thread() == tcb_ptr)
      {
        lk_ptr -> release();
      }
    }


    schedule_threads(&signal_mask_state);

    return VM_STATUS_SUCCESS;
  }

  TVMStatus VMThreadID(TVMThreadIDRef threadref)
  {
    TMachineSignalState signal_mask_state;
    MachineSuspendSignals(&signal_mask_state);

    if (threadref == NULL)
    {
      MachineResumeSignals(&signal_mask_state);
      return VM_STATUS_ERROR_INVALID_PARAMETER;
    }

    *threadref = global_state.cur_thread_ptr -> get_thread_id();

    MachineResumeSignals(&signal_mask_state);
    return VM_STATUS_SUCCESS;
  }

  TVMStatus VMThreadState(TVMThreadID thread, TVMThreadStateRef stateref)
  {
    TMachineSignalState signal_mask_state;
    MachineSuspendSignals(&signal_mask_state);

    if (stateref == NULL)
    {
      MachineResumeSignals(&signal_mask_state);
      return VM_STATUS_ERROR_INVALID_PARAMETER;
    }

    TCB *tcb_ptr = global_state.all_threads[thread];
    if (tcb_ptr == NULL)
    {
      MachineResumeSignals(&signal_mask_state);
      return VM_STATUS_ERROR_INVALID_ID;
    }

    *stateref = tcb_ptr -> thread_state;

    MachineResumeSignals(&signal_mask_state);
    return VM_STATUS_SUCCESS;
  }

  TVMStatus VMThreadSleep(TVMTick tick)
  {
    TMachineSignalState signal_mask_state;
    MachineSuspendSignals(&signal_mask_state);

    if (tick == VM_TIMEOUT_INFINITE)
    {
      MachineResumeSignals(&signal_mask_state);
      return VM_STATUS_ERROR_INVALID_PARAMETER;
    }

    TCB* cur_th_ptr = global_state.cur_thread_ptr;
    cur_th_ptr -> thread_state = VM_THREAD_STATE_WAITING;

    cur_th_ptr -> enable_time = global_state.current_time +
      (tick == VM_TIMEOUT_IMMEDIATE ? 0 : tick);

    global_state.wait_list.push(cur_th_ptr);
    schedule_threads(&signal_mask_state);

    return VM_STATUS_SUCCESS;
  }

































  Lock::Lock()
  {
    TMachineSignalState signal_mask_state;
    MachineSuspendSignals(&signal_mask_state);

    lock_id = Lock::lock_count;
    Lock::lock_count += 1;

    MachineResumeSignals(&signal_mask_state);
  }

  TVMMutexID Lock::get_id()
  {
    return lock_id;
  }

  bool Lock::is_free()
  {
    return !is_acquired;
  }

  TCB* Lock::get_owner_thread()
  {
    return thread_holder;
  }

  bool Lock::acquire(TVMTick timeout, TMachineSignalState* mask)
  {
    if (!is_acquired)
    {
      is_acquired = true;
      thread_holder = global_state.cur_thread_ptr;
      MachineResumeSignals(mask);
      return true;
    }
    else
    {
      if (timeout == VM_TIMEOUT_IMMEDIATE)
      {
        MachineResumeSignals(mask);
        return false;
      }

      TCB* cur_th_ptr = global_state.cur_thread_ptr;
      cur_th_ptr -> thread_state = VM_THREAD_STATE_WAITING;

      if (timeout != VM_TIMEOUT_INFINITE)
      {
        cur_th_ptr -> enable_time = global_state.current_time + timeout;
      }
      else
      {
        unsigned int end_of_time = 0;
        end_of_time -= 1;

        cur_th_ptr -> enable_time = end_of_time;
      }

      global_state.wait_list.push(cur_th_ptr);
      wait_list.push(cur_th_ptr);

      schedule_threads(mask);

      TMachineSignalState signal_mask_state;
      MachineSuspendSignals(&signal_mask_state);

      bool acquired_lock = global_state.cur_thread_ptr == thread_holder;

      std::stack<TCB*> tmp_stack;
      TCB* tmp_tcb_ptr = NULL;

      while (global_state.wait_list.size() != 0)
      {
        tmp_tcb_ptr = global_state.wait_list.top();
        global_state.wait_list.pop();
        if (tmp_tcb_ptr != global_state.cur_thread_ptr)
        {
          tmp_stack.push(tmp_tcb_ptr);
        }
      }

      while (tmp_stack.size() != 0)
      {
        tmp_tcb_ptr = tmp_stack.top();
        tmp_stack.pop();
        global_state.wait_list.push(tmp_tcb_ptr);
      }

      while (wait_list.size() != 0)
      {
        tmp_tcb_ptr = wait_list.top();
        wait_list.pop();
        if (tmp_tcb_ptr != global_state.cur_thread_ptr)
        {
          tmp_stack.push(tmp_tcb_ptr);
        }
      }

      while (tmp_stack.size() != 0)
      {
        tmp_tcb_ptr = tmp_stack.top();
        tmp_stack.pop();
        wait_list.push(tmp_tcb_ptr);
      }

      MachineResumeSignals(&signal_mask_state);
      // could be returned here due to acquiring
      // could be return here due to timeout
      return acquired_lock;
    }
  }

  void Lock::release()
  {
    if (wait_list.size() == 0)
    {
      is_acquired = false;
      thread_holder = NULL;
    }
    else
    {
      TCB* next_tcb = wait_list.top();
      wait_list.pop();
      next_tcb -> enable_time = 0;

      std::stack<TCB*> tmp_stack;
      TCB* tmp_tcb_ptr = NULL;

      while (global_state.wait_list.size() != 0)
      {
        tmp_tcb_ptr = global_state.wait_list.top();
        global_state.wait_list.pop();
        tmp_stack.push(tmp_tcb_ptr);
      }

      while (tmp_stack.size() != 0)
      {
        tmp_tcb_ptr = tmp_stack.top();
        tmp_stack.pop();
        global_state.wait_list.push(tmp_tcb_ptr);
      }

      is_acquired = true;
      thread_holder = next_tcb;
    }
  }

  TVMStatus VMMutexCreate(TVMMutexIDRef mutexref)
  {
    TMachineSignalState signal_mask_state;
    MachineSuspendSignals(&signal_mask_state);

    if (mutexref == NULL)
    {
      MachineResumeSignals(&signal_mask_state);
      return VM_STATUS_ERROR_INVALID_PARAMETER;
    }

    Lock *lock = new Lock();
    *mutexref = lock -> get_id();
    global_state.all_locks[*mutexref] = lock;

    MachineResumeSignals(&signal_mask_state);
    return VM_STATUS_SUCCESS;
  }

  TVMStatus VMMutexDelete(TVMMutexID mutex)
  {
    TMachineSignalState signal_mask_state;
    MachineSuspendSignals(&signal_mask_state);

    Lock *lock_ptr = global_state.all_locks[mutex];

    if (lock_ptr == NULL)
    {
      MachineResumeSignals(&signal_mask_state);
      return VM_STATUS_ERROR_INVALID_ID;
    }

    if (!(lock_ptr -> is_free()))
    {
      MachineResumeSignals(&signal_mask_state);
      return VM_STATUS_ERROR_INVALID_STATE;
    }

    delete lock_ptr;
    global_state.all_locks.erase(mutex);

    MachineResumeSignals(&signal_mask_state);
    return VM_STATUS_SUCCESS;
  }

  TVMStatus VMMutexQuery(TVMMutexID mutex, TVMThreadIDRef ownerref)
  {
    TMachineSignalState signal_mask_state;
    MachineSuspendSignals(&signal_mask_state);

    if (ownerref == NULL)
    {
      MachineResumeSignals(&signal_mask_state);
      return VM_STATUS_ERROR_INVALID_PARAMETER;
    }

    Lock *lock_ptr = global_state.all_locks[mutex];

    if (lock_ptr == NULL)
    {
      MachineResumeSignals(&signal_mask_state);
      return VM_STATUS_ERROR_INVALID_ID;
    }

    *ownerref = lock_ptr -> is_free() ? VM_THREAD_ID_INVALID :
      lock_ptr -> get_owner_thread() -> get_thread_id();


    MachineResumeSignals(&signal_mask_state);
    return VM_STATUS_SUCCESS;
  }

  TVMStatus VMMutexAcquire(TVMMutexID mutex, TVMTick timeout)
  {
    TMachineSignalState signal_mask_state;
    MachineSuspendSignals(&signal_mask_state);

    Lock *lock_ptr = global_state.all_locks[mutex];

    if (lock_ptr == NULL)
    {
      MachineResumeSignals(&signal_mask_state);
      return VM_STATUS_ERROR_INVALID_ID;
    }

    if (!(lock_ptr -> acquire(timeout, &signal_mask_state)))
    {
      return VM_STATUS_FAILURE;
    }

    return VM_STATUS_SUCCESS;
  }

  TVMStatus VMMutexRelease(TVMMutexID mutex)
  {
    TMachineSignalState signal_mask_state;
    MachineSuspendSignals(&signal_mask_state);

    Lock *lock_ptr = global_state.all_locks[mutex];

    if (lock_ptr == NULL)
    {
      MachineResumeSignals(&signal_mask_state);
      return VM_STATUS_ERROR_INVALID_ID;
    }

    if (lock_ptr -> get_owner_thread() != global_state.cur_thread_ptr)
    {
      MachineResumeSignals(&signal_mask_state);
      return VM_STATUS_ERROR_INVALID_STATE;
    }

    lock_ptr -> release();

    MachineResumeSignals(&signal_mask_state);
    return VM_STATUS_SUCCESS;
  }









  // -+
  TVMStatus VMFileRead(int filedescriptor, void *data, int *length)
  {
    TMachineSignalState signal_mask_state;
    MachineSuspendSignals(&signal_mask_state);

    if ((filedescriptor >= 0) && (filedescriptor <= 2))
    {
      return OG_VMFileRead(&signal_mask_state, filedescriptor, data, length);
    }

    MachineResumeSignals(&signal_mask_state);
    return VM_STATUS_FAILURE;
  }

  // -+
  TVMStatus VMFileWrite(int filedescriptor, void *data, int *length)
  {
    TMachineSignalState signal_mask_state;
    MachineSuspendSignals(&signal_mask_state);

    if ((filedescriptor >= 0) && (filedescriptor <= 2))
    {
      return OG_VMFileWrite(&signal_mask_state, filedescriptor, data, length);
    }

    std::cout << ">> [VMFileWrite] Error. File Descriptor: " << filedescriptor << "\n";

    MachineResumeSignals(&signal_mask_state);
    return VM_STATUS_FAILURE;
  }






  // +
  struct OG_FileArg
  {
    int result = 0;
    TVMThreadID thread_id = TCB::INVALID_ID;
  };

  // +
  void OG_FileCB(void* arg, int result)
  {

    TMachineSignalState signal_mask_state;
    MachineSuspendSignals(&signal_mask_state);

    OG_FileArg* arg_ptr = static_cast<OG_FileArg*>(arg);
    arg_ptr -> result = result;

    if (arg_ptr -> thread_id != TCB::INVALID_ID)
    {
      TCB* thread_ptr = global_state.all_threads[arg_ptr -> thread_id];

      if ((thread_ptr != NULL) && (thread_ptr -> thread_state ==
        VM_THREAD_STATE_WAITING))
      {

        thread_ptr -> enable_time = 0;
        thread_ptr -> thread_state = VM_THREAD_STATE_READY;
        global_state.ready_list.push(thread_ptr);
      }
    }

    MachineResumeSignals(&signal_mask_state);
  }

  // +
  TVMStatus OG_VMFileOpen(TMachineSignalStateRef mask, const char *filename, int flags, int mode, int *filedescriptor)
  {
    if (filename == NULL || filedescriptor == NULL)
    {
      MachineResumeSignals(mask);
      return VM_STATUS_ERROR_INVALID_PARAMETER;
    }

    TCB* cur_th_ptr = global_state.cur_thread_ptr;
    cur_th_ptr -> thread_state = VM_THREAD_STATE_WAITING;

    volatile OG_FileArg arg;
    arg.thread_id = cur_th_ptr -> get_thread_id();
    void *arg_ptr = (void*) (&arg);

    MachineFileOpen(filename, flags, mode, OG_FileCB, arg_ptr);
    schedule_threads(mask);

    if (arg.result < 0)
    {
      return VM_STATUS_FAILURE;
    }

    *filedescriptor = arg.result;

    return VM_STATUS_SUCCESS;

  }

  // +
  TVMStatus OG_VMFileClose(TMachineSignalStateRef mask, int filedescriptor)
  {
    TCB* cur_th_ptr = global_state.cur_thread_ptr;
    cur_th_ptr -> thread_state = VM_THREAD_STATE_WAITING;

    volatile OG_FileArg arg;
    arg.thread_id = cur_th_ptr -> get_thread_id();
    void *arg_ptr = (void*) (&arg);

    MachineFileClose(filedescriptor, OG_FileCB, arg_ptr);
    schedule_threads(mask);

    if (arg.result < 0)
    {
      return VM_STATUS_FAILURE;
    }

    return VM_STATUS_SUCCESS;
  }

  // +
  TVMStatus OG_VMFileRead(TMachineSignalStateRef mask, int filedescriptor, void *data, int *length)
  {
    TMachineSignalState** mask_ptr = &mask;

    if (data == NULL || length == NULL)
    {
      MachineResumeSignals(mask);
      return VM_STATUS_ERROR_INVALID_PARAMETER;
    }

    int expected_total_bytes = *length;
    int bytes_read_total = 0;
    bool is_failure = false;
    bool is_last_read = false;
    char* char_data = static_cast<char*>(data);

    do
    {
      TCB* cur_th_ptr = global_state.cur_thread_ptr;
      cur_th_ptr -> thread_state = VM_THREAD_STATE_WAITING;

      volatile OG_FileArg arg;
      void *arg_ptr = (void*) &arg;

      arg.thread_id = cur_th_ptr -> get_thread_id();

      int mem_cell_id = -1;
      char *address_ptr = NULL;

      global_state.mem_mgr_ptr -> allocate_cell(&mem_cell_id, &address_ptr, mask_ptr);

      int num_of_bytes_to_read = expected_total_bytes - bytes_read_total;
      if (num_of_bytes_to_read > 512)
      {
        num_of_bytes_to_read = 512;
      }
      else
      {
        is_last_read = true;
      }

      MachineFileRead(filedescriptor, address_ptr, num_of_bytes_to_read, OG_FileCB, arg_ptr);
      schedule_threads(mask);

      MachineSuspendSignals(mask);

      global_state.mem_mgr_ptr -> free_memory_cell(mem_cell_id);


      if (arg.result < 0)
      {
        is_failure = true;
      }
      else
      {
        std::memcpy(char_data + bytes_read_total, address_ptr, arg.result);
        bytes_read_total += arg.result;

        if (arg.result < num_of_bytes_to_read)
        {
          is_last_read = true;
        }
      }


    }
    while(!is_last_read && !is_failure);

    *length = bytes_read_total;

    if (is_failure)
    {
      MachineResumeSignals(mask);
      return VM_STATUS_FAILURE;
    }

    MachineResumeSignals(mask);
    return VM_STATUS_SUCCESS;
  }

  // +
  TVMStatus OG_VMFileWrite(TMachineSignalStateRef mask, int filedescriptor, void *data, int *length)
  {
    TMachineSignalState** mask_ptr = &mask;

    if (data == NULL || length == NULL)
    {
      MachineResumeSignals(mask);
      return VM_STATUS_ERROR_INVALID_PARAMETER;
    }

    int expected_total_bytes = *length;
    int bytes_written_total = 0;
    bool is_failure = false;
    bool is_last_write = false;
    char* char_data = static_cast<char*>(data);

    do
    {
      // Waiting
      TCB* cur_th_ptr = global_state.cur_thread_ptr;
      cur_th_ptr -> thread_state = VM_THREAD_STATE_WAITING;

      volatile OG_FileArg arg;
      void *arg_ptr = (void*) &arg;

      arg.thread_id = cur_th_ptr -> get_thread_id();

      int mem_cell_id = -1;
      char *address_ptr = NULL;
      char *data_offset = char_data + bytes_written_total;

      global_state.mem_mgr_ptr -> allocate_cell(&mem_cell_id, &address_ptr, mask_ptr);

      int num_of_bytes_to_write = expected_total_bytes - bytes_written_total;
      if (num_of_bytes_to_write > 512)
      {
        num_of_bytes_to_write = 512;
      }
      else
      {
        is_last_write = true;
      }

      std::memcpy(address_ptr, static_cast<void*>(data_offset), num_of_bytes_to_write);

      MachineFileWrite(
        filedescriptor, address_ptr, num_of_bytes_to_write, OG_FileCB, arg_ptr);

      schedule_threads(mask);

      MachineSuspendSignals(mask);

      global_state.mem_mgr_ptr -> free_memory_cell(mem_cell_id);

      if (arg.result < 0)
      {
        is_failure = true;
      }
      else
      {
        bytes_written_total += arg.result;

        if (arg.result < num_of_bytes_to_write)
        {
          is_last_write = true;
        }
      }
    }
    while(!is_last_write && !is_failure);

    *length = bytes_written_total;

    if (is_failure)
    {
      MachineResumeSignals(mask);
      return VM_STATUS_FAILURE;
    }

    MachineResumeSignals(mask);
    return VM_STATUS_SUCCESS;
  }

  // +
  TVMStatus OG_VMFileSeek(TMachineSignalStateRef mask, int filedescriptor, int offset, int whence, int *newoffset)
  {

    TCB* cur_th_ptr = global_state.cur_thread_ptr;
    cur_th_ptr -> thread_state = VM_THREAD_STATE_WAITING;

    volatile OG_FileArg arg;
    arg.thread_id = cur_th_ptr -> get_thread_id();
    void *arg_ptr = (void*) (&arg);

    MachineFileSeek(filedescriptor, offset, whence, OG_FileCB, arg_ptr);
    schedule_threads(mask);

    if (arg.result < 0)
    {
      return VM_STATUS_FAILURE;
    }

    if (newoffset != NULL)
    {
      *newoffset = arg.result;
    }

    return VM_STATUS_SUCCESS;
  }





  TVMStatus VMDirectoryOpen(const char *dirname, int *dirdescriptor)
  {
    return VM_STATUS_FAILURE;
  }


  // +-
  // @TODO: locks?
  TVMStatus VMDirectoryCurrent(char *abspath)
  {
    TMachineSignalState signal_mask_state;
    MachineSuspendSignals(&signal_mask_state);

    if (abspath == NULL)
    {
      MachineResumeSignals(&signal_mask_state);
      return VM_STATUS_ERROR_INVALID_PARAMETER;
    }

    global_state.fat.current_working_directory(abspath);
    std::cout << "HERE\n";

    MachineResumeSignals(&signal_mask_state);
    return VM_STATUS_SUCCESS;
  }

  // -
  TVMStatus VMDirectoryChange(const char *path)
  {
    return VM_STATUS_FAILURE;
  }
}
