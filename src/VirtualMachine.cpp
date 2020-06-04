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

  TVMStatus OG_VMMutexCreate(TMachineSignalStateRef mask, TVMMutexIDRef mutexref);
  TVMStatus OG_VMMutexDelete(TMachineSignalStateRef mask, TVMMutexID mutex);
  TVMStatus OG_VMMutexQuery(TMachineSignalStateRef mask, TVMMutexID mutex, TVMThreadIDRef ownerref);
  TVMStatus OG_VMMutexAcquire(TMachineSignalStateRef mask, TVMMutexID mutex, TVMTick timeout);
  TVMStatus OG_VMMutexRelease(TMachineSignalStateRef mask, TVMMutexID mutex);

  void VMStringCopy(char *dest, const char *src);

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




  // https://man7.org/linux/man-pages/man2/open.2.html
  // O_CREAT | O_TRUNC | O_RDWR, 0644,
  // O_RDONLY, 0644,
  // must flag must include one of: O_RDONLY, O_WRONLY, or O_RDWR.
  // Additional:
  // O_CREAT - If pathname does not exist, create it as a regular file.
  // O_TRUNC - if pathname exists, is a regular file,
  //              access mode allows writing (is O_RDWR or O_WRONLY)
  // it will be truncated to length 0
  // mode: user, group, others
  // 400 - read, 200 - write, 100 - execute
  bool PARSE_FLAGS(
    int flags, bool *can_read, bool *can_write, bool *must_exist, bool *to_clear)
  {
    if ((flags & O_RDWR) == O_RDWR)
    {
      *can_read = true;
      *can_write = true;
    }
    else if ((flags & O_WRONLY) == O_WRONLY)
    {
      *can_write = true;
    }
    else if ((flags & O_RDONLY) == O_RDONLY)
    {
      *can_read = true;
    }

    if ((flags & O_TRUNC) == O_TRUNC)
    {
      *to_clear = true;
    }

    if ((flags & O_CREAT) == O_CREAT)
    {
      *must_exist = false;
    }

    return *can_read || *can_write;
  }

  /*
  - value_types:
  -- 0 - int8_t (1 byte)
  -- 1 - uint8_t (1 byte)
  -- 2 - int16_t
  -- 3 - uint16_t
  -- 4 - int32_t
  -- 5 - uint32_t
  */
  void TO_INT_FROM_LITTLE_ENDIAN_BUFFER(uint8_t *byte_buffer, int buffer_len, void *value, uint8_t type)
  {
    if (type == 0)
    {
      int8_t* ptr = (int8_t*) value;
      for (int i = buffer_len - 1; i >= 0; i--)
      {
        *ptr = (*ptr << 8) + *(byte_buffer + i);
      }
    }
    else if (type == 1)
    {
      uint8_t* ptr = (uint8_t*) value;
      for (int i = buffer_len - 1; i >= 0; i--)
      {
        *ptr = (*ptr << 8) + *(byte_buffer + i);
      }
    }
    else if (type == 2)
    {
      int16_t* ptr = (int16_t*) value;
      for (int i = buffer_len - 1; i >= 0; i--)
      {
        *ptr = (*ptr << 8) + *(byte_buffer + i);
      }
    }
    else if (type == 3)
    {
      uint16_t* ptr = (uint16_t*) value;
      for (int i = buffer_len - 1; i >= 0; i--)
      {
        *ptr = (*ptr << 8) + *(byte_buffer + i);
      }
    }
    else if (type == 4)
    {
      int32_t* ptr = (int32_t*) value;
      for (int i = buffer_len - 1; i >= 0; i--)
      {
        *ptr = (*ptr << 8) + *(byte_buffer + i);
      }
    }
    else if (type == 5)
    {
      uint32_t* ptr = (uint32_t*) value;
      for (int i = buffer_len - 1; i >= 0; i--)
      {
        *ptr = (*ptr << 8) + *(byte_buffer + i);
      }
    }
  }

  void TO_LITTLE_ENDIAN_BUFFER_FROM_INT(
    uint8_t *byte_buffer, int buffer_len, void *ptr_value, uint8_t type)
  {
    uint8_t MASK = 0xff;

    if (type == 0)
    {
      int8_t value = (int8_t) (*(int8_t*)ptr_value); // 8 bits - 1 byte

      for (int i = 0; i < buffer_len; i++)
      {
        uint8_t last_val = value & MASK;
        value = value >> 8;
        byte_buffer[i] = last_val;
      }
    }
    else if (type == 1)
    {
      uint8_t value = (uint8_t) (*(uint8_t*)ptr_value); // 8 bits - 1 byte

      for (int i = 0; i < buffer_len; i++)
      {
        uint8_t last_val = value & MASK;
        value = value >> 8;
        byte_buffer[i] = last_val;
      }
    }
    else if (type == 2)
    {
      int16_t value = (int16_t) (*(int16_t*)ptr_value); // 16 bits - 2 bytes

      for (int i = 0; i < buffer_len; i++)
      {
        uint8_t last_val = value & MASK;
        value = value >> 8;
        byte_buffer[i] = last_val;
      }
    }
    else if (type == 3)
    {
      uint16_t value = (uint16_t) (*(uint16_t*)ptr_value); // 16 bits - 2 bytes

      for (int i = 0; i < buffer_len; i++)
      {
        uint8_t last_val = value & MASK;
        value = value >> 8;
        byte_buffer[i] = last_val;
      }
    }
    else if (type == 4)
    {
      int32_t value = (int32_t) (*(int32_t*)ptr_value); // 32 bits - 4 bytes

      for (int i = 0; i < buffer_len; i++)
      {
        uint8_t last_val = value & MASK;
        value = value >> 8;
        byte_buffer[i] = last_val;
      }
    }
    else if (type == 5)
    {
      uint32_t value = (uint32_t) (*(uint32_t*)ptr_value); // 32 bits - 4 bytes

      for (int i = 0; i < buffer_len; i++)
      {
        uint8_t last_val = value & MASK;
        value = value >> 8;
        byte_buffer[i] = last_val;
      }
    }
  }


  uint16_t DAY_MASK    = 0b0000000000011111;
  uint16_t MONTH_MASK  = 0b0000000111100000;
  uint16_t YEAR_MASK   = 0b1111111000000000;
  uint8_t  MONTH_SHIFT = 5;
  uint8_t  YEAR_SHIFT  = 9;
  uint16_t YEAR_OFFSET = 1980;

  uint16_t HOURS_MASK    = 0b1111100000000000;
  uint16_t MINUTES_MASK  = 0b0000011111100000;
  uint16_t SECONDS_MASK  = 0b0000000000011111;
  uint8_t  HOURS_SHIFT   = 11;
  uint8_t  MINUTES_SHIFT = 5;


  void BYTE_TO_DATE_TIME_3(SVMDateTimeRef date_time, uint16_t date, uint16_t time)
  {
    date_time -> DYear = ((date & YEAR_MASK) >> YEAR_SHIFT) + YEAR_OFFSET;
    date_time -> DMonth = ((date & MONTH_MASK) >> MONTH_SHIFT);
    date_time -> DDay = date & DAY_MASK;

    date_time -> DHour = (time & HOURS_MASK) >> HOURS_SHIFT;
    date_time -> DMinute = (time & MINUTES_MASK) >> MINUTES_SHIFT;
    date_time -> DSecond = (time & SECONDS_MASK) * 2;
  };

  void BYTE_TO_DATE_TIME_4(
    SVMDateTimeRef date_time, uint16_t date, uint16_t time, uint8_t time_tenth)
  {
    BYTE_TO_DATE_TIME_3(date_time, date, time);

    date_time -> DSecond += (time_tenth / 100);
    date_time -> DHundredth = time_tenth % 100;
  }





// needs to have only legal chars; legal short name
std::string SHORT_NAME_TO_BUFFER(char* fname)
{
  std::cout << "UNEXPECTED TO BE HERE\n";
}

bool IS_LEGAL_CHAR_IN_LONG_NAME(char& ch)
{
  if (ch <= 0x20)
  {
    return false;
  }

  switch (ch)
  {
    // illegal values
    case 0x22:
    case 0x2A:
    case 0x2B:
    case 0x2C:
    case 0x2E:
    case 0x2F:
    case 0x3A:
    case 0x3B:
    case 0x3C:
    case 0x3D:
    case 0x3E:
    case 0x3F:
    case 0x5B:
    case 0x5C:
    case 0x5D:
    case 0x7C:
      return false;
    // legals values
    default:
      return true;
  }
}

  // max length = 12 (8 name, 1 dot, 3 ext)
  // needs to filter bad chars
  std::string LONG_TO_SHORT_NAME(char* long_name)
  {
    char short_name[13] = {
      '\0', '\0', '\0', '\0', '\0', '\0',
      '\0', '\0', '\0', '\0', '\0', '\0', '\0'};

    int short_name_i = 0;
    int long_name_i = 0;
    bool is_long_name_extension = false;
    int short_name_extension_len = -1;

    // handle short_name length
    while (true)
    {
      // reached end of long name
      if (long_name[long_name_i] == 0x00)
      {
        break;
      }

      if (is_long_name_extension && (short_name_extension_len >= 3))
      {
        break;
      }

      // '.' in long name: if 1st time, add to short name. Otherwise, ignore
      if (long_name[long_name_i] == '.')
      {
        is_long_name_extension = true;
        long_name_i += 1;
        continue;
      }

      // if already got 8 chars in file name and still not in long name extesion
      if ((!is_long_name_extension) && (short_name_i > 7))
      {
        long_name_i += 1;
        continue;
      }


      // invalid char in long name - ignore
      if (!IS_LEGAL_CHAR_IN_LONG_NAME(long_name[long_name_i]))
      {
        long_name_i += 1;
        continue;
      }

      // only valid chars here. no '.'
      // case 1: long name - actual name
      // case 2: long name - extension

      if (is_long_name_extension && (short_name_extension_len == -1))
      {
        short_name[short_name_i] = '.';
        short_name_i += 1;
        short_name_extension_len = 0;
        continue;
      }

      short_name[short_name_i] = std::toupper(long_name[long_name_i]);
      if (is_long_name_extension)
      {
        short_name_extension_len += 1;
      }
      short_name_i += 1;
      long_name_i += 1;
    }

    return std::string(short_name);
  }

  // expects 11 bytes
  // +
  std::string BUF_TO_SHORT_NAME(char* buf)
  {
    char short_name[13] = {
      '\0', '\0', '\0', '\0', '\0', '\0',
      '\0', '\0', '\0', '\0', '\0', '\0', '\0'};

    int i = 0;
    for (int j = 0; j < 8; j++)
    {
      if ((j == 0) && (buf[j] == 0x05))
      {
        short_name[i] = 0xE5;
        i += 1;
      }
      else if (buf[j] != 0x20)
      {
        short_name[i] = buf[j];
        i += 1;
      }
    }

    if (buf[8] != 0x20)
    {
      short_name[i] = '.';
      i += 1;
    }

    for (int j = 8; j < 11; j++)
    {
      if (buf[j] != 0x20)
      {
        short_name[i] = buf[j];
        i += 1;
      }
    }

    return std::string(short_name);
  }



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
























  class DirEntry
  {
    std::string short_name;
    uint16_t first_cluster_number;
    bool file = false;
    bool read_only = false;

    SVMDirectoryEntry internal_entry;

  public:
    DirEntry(char* buffer);

    std::string get_short_name() { return short_name; }
    bool is_file() { return file; };
    bool is_read_only() { return read_only; }

    void get_internal_entry(SVMDirectoryEntryRef ptr)
    {
      *ptr = internal_entry;
    }
  };

  DirEntry::DirEntry(char* buffer)
  {

    short_name = BUF_TO_SHORT_NAME(buffer);
    // short name
    VMStringCopy(internal_entry.DShortFileName, short_name.c_str());
    // size
    TO_INT_FROM_LITTLE_ENDIAN_BUFFER((uint8_t*)(buffer + 28), 4, (void*) &internal_entry.DSize, 5);
    // attributes
    internal_entry.DAttributes = buffer[11];

    // not a file; not an archive
    file =
      ((buffer[11] & VM_FILE_SYSTEM_ATTR_DIRECTORY) == 0) &&
      ((buffer[11] & VM_FILE_SYSTEM_ATTR_ARCHIVE) == 0);
    read_only = (buffer[11] & VM_FILE_SYSTEM_ATTR_READ_ONLY) != 0;


    uint16_t creat_date;
    uint16_t creat_time;
    uint8_t create_time_tenth;
    uint16_t access_time = 0;
    uint16_t access_date;
    uint16_t write_date;
    uint16_t write_time;

    TO_INT_FROM_LITTLE_ENDIAN_BUFFER((uint8_t*)(buffer + 16), 2, (void*) &creat_date, 3);
    TO_INT_FROM_LITTLE_ENDIAN_BUFFER((uint8_t*)(buffer + 14), 2, (void*) &creat_time, 3);
    TO_INT_FROM_LITTLE_ENDIAN_BUFFER((uint8_t*)(buffer + 13), 1, (void*) &create_time_tenth, 1);
    TO_INT_FROM_LITTLE_ENDIAN_BUFFER((uint8_t*)(buffer + 18), 2, (void*) &access_date, 3);
    TO_INT_FROM_LITTLE_ENDIAN_BUFFER((uint8_t*)(buffer + 24), 2, (void*) &write_date, 3);
    TO_INT_FROM_LITTLE_ENDIAN_BUFFER((uint8_t*)(buffer + 22), 2, (void*) &write_time, 3);

    BYTE_TO_DATE_TIME_4(
      &(internal_entry.DCreate), creat_date, creat_time, create_time_tenth);
    BYTE_TO_DATE_TIME_3(&(internal_entry.DAccess), access_date, access_time);
    BYTE_TO_DATE_TIME_3(&(internal_entry.DModify), write_date, write_time);


    TO_INT_FROM_LITTLE_ENDIAN_BUFFER(
      (uint8_t*)(buffer + 26), 2, (void*) &first_cluster_number, 3);
  }

  class FdOffset
  {
  public:
    int fd = -1;
    int offset = -1;
  };


  class FatFS
  {
    inline static int last_file_fd = 100;
    inline static int last_dir_fd = 101;

    std::string mount_name;
    std::string abs_path = "/";

    bool is_mount_lock_free = true;
    TVMThreadID mount_lock_owner_id = 0;
    TVMMutexID mount_lock_id;
    FdOffset mount_fd_offset;
    int root_dir_eof_offset = -1;
    std::vector<DirEntry*> root_dir_entries;
    std::map<int, int> dir_fds;

    bool is_fat_lock_free = true;
    TVMThreadID fat_lock_owner_id = 0;
    TVMMutexID fat_lock_id;
    std::vector<uint16_t> fat_arr;

    // METADATA
    uint16_t bytes_per_sector;
    uint8_t sectors_per_cluster;
    uint16_t reserved_sectors_count;
    uint8_t fats_count;
    uint16_t root_entries_count;
    uint32_t total_sectors_count;
    uint32_t root_dir_sectors_count;
    uint8_t media_value;
    uint16_t eof_value;
    uint16_t sectors_per_fat;
    uint32_t volume_serial_number;
    std::string volume_label;

    uint32_t fat_size;

    bool is_fat_array_access_allowed();
    bool is_mount_access_allowed();

    // n = 0 -> fat array offset
    // n = 1 -> root dir offset
    // n >= 2 -> data offset
    int get_offset_of_cluster(int n);

    bool init_metadata(TMachineSignalStateRef mask_ptr);
    bool init_fat_array(TMachineSignalStateRef mask_ptr);
    bool init_root_dir(TMachineSignalStateRef mask_ptr);

    void debug_print_bpb_metadata();
  public:
    bool init(const char *mount_name_ptr);

    void acquire_fat_lock(TMachineSignalStateRef mask_ptr);
    void release_fat_lock(TMachineSignalStateRef mask_ptr);
    std::vector<uint16_t>* get_fat_array();

    void acquire_mount_lock(TMachineSignalStateRef mask_ptr);
    void release_mount_lock(TMachineSignalStateRef mask_ptr);
    FdOffset* get_mount_fd_offset();
    std::vector<DirEntry*>* get_root_dir_entries();

    void get_current_working_directory(char *dst);
    int generate_new_fd(bool is_dir);

    bool init_new_fd(int fd);
    bool delete_fd(int fd);
    bool read_fd_dir(int fd, SVMDirectoryEntryRef dir);
    bool rewind_fd_dir(int fd);

    void file_entry_exists(std::string short_name, bool* exists, bool* is_error);
    bool allocate_new_cluster(TMachineSignalStateRef mask_ptr, int* id);
    bool store_sectors_into_memory(TMachineSignalStateRef mask_ptr, FdOffset* fd_offset, uint8_t *buffer, int sect_index, int sect_num);
  };

















  class GlobalState
  {
  public:
    std::priority_queue<TCB*, std::vector<TCB*>, TcbPriorityCompare > ready_list;
    std::priority_queue<TCB*, std::vector<TCB*>, TcbWaitCompare > wait_list;

    std::map<TVMThreadID, TCB* > all_threads;
    std::map<TVMMutexID, Lock*> all_locks;
    TCB *cur_thread_ptr = NULL;
    MemoryManager *mem_mgr_ptr = NULL;
    FatFS fat_fs;

    int tick_ms = 0;
    TVMTick current_time = 0;
    // FAT fat_fs;

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



  // @TODO: locks
  // meta = 0
  // fat = 1 - 34
  // root dir = 35-66
  // data = 67 -> ...
  // ASSUMPTION: locks are acquired already
  bool FatFS::store_sectors_into_memory(
    TMachineSignalStateRef mask_ptr, FdOffset* fd_offset, uint8_t *buffer, int sect_index, int sect_num)
  {
    if (fd_offset == NULL)
    {
      return false;
    }

    int buffer_len = bytes_per_sector * sect_num;
    int expected_offset = sect_index * bytes_per_sector;

    TVMStatus res = OG_VMFileSeek(
      mask_ptr, fd_offset -> fd,
      expected_offset, 0, &(fd_offset -> offset)
    );
    MachineSuspendSignals(mask_ptr);

    if ((res != VM_STATUS_SUCCESS) || (fd_offset -> offset != expected_offset))
    {
      std::cout
        << "[FatFS] can't store_sectors_into_memory(): "
        << "failed to seek to proper offset. "
        << "Status = " << res << ". "
        << "Expected offset = " << expected_offset << ". "
        << "Actual offset = " << fd_offset -> offset << ".\n";

      return false;
    }

    int did_write_bytes = buffer_len;
    res = OG_VMFileWrite(mask_ptr, fd_offset -> fd, (void*) buffer, &did_write_bytes);
    MachineSuspendSignals(mask_ptr);

    if (res == VM_STATUS_SUCCESS)
    {
      fd_offset -> offset += did_write_bytes;
    }

    if ((res != VM_STATUS_SUCCESS) || (did_write_bytes != buffer_len))
    {
      std::cout
        << "[FatFS] can't store_sectors_into_memory(): "
        << "failed to write all data. "
        << "Status = " << res << ". "
        << "Expected bytes written = " << buffer_len << ". "
        << "Actual number = " << did_write_bytes << ".\n";

      return false;
    }

    return true;
  }


  bool FatFS::allocate_new_cluster(TMachineSignalStateRef mask_ptr, int* id)
  {
    if (!is_fat_array_access_allowed())
    {
      return false;
    }

    if (!is_mount_access_allowed())
    {
      return false;
    }

    FdOffset* mount = get_mount_fd_offset();
    if (mount == NULL)
    {
      return false;
    }

    int free_index = 2;
    while(fat_arr[free_index] != 0)
    {
      free_index += 1;
    };

    // allocate
    fat_arr[free_index] = 0xffff;

    int fat_entries_in_sector = bytes_per_sector / 2;

    // 2 bytes per entry
    // get base offset of the correct sector:
    int fat_offset = get_offset_of_cluster(0);
    fat_offset += (2 * free_index);
    int sector_index = fat_offset / bytes_per_sector;

    uint8_t buffer[bytes_per_sector];

    int start_fat_array_index =
      (free_index / fat_entries_in_sector) * fat_entries_in_sector;

    for (int i = 0; i < fat_entries_in_sector; i++)
    {
      uint16_t fat_arr_val = fat_arr[start_fat_array_index + i];
      TO_LITTLE_ENDIAN_BUFFER_FROM_INT(buffer + 2*i, 2, (void*) &fat_arr_val, 3);
    }


    // @TODO: allocate a lock to write into sector
    if (!store_sectors_into_memory(mask_ptr, mount, buffer, sector_index, 1))
    {
      return false;
    }

    *id = free_index;
    return true;
  }


  void FatFS::file_entry_exists(std::string short_name, bool* exists, bool* is_error)
  {
    if (!is_mount_access_allowed())
    {
      *is_error = true;
      return;
    }

    *exists = false;

    for(auto it = root_dir_entries.begin(); it != root_dir_entries.end(); ++it)
    {
      if (((*it) -> get_short_name()) == short_name)
      {
        if ((*it) -> is_file())
        {
          *exists = true;
        }
      }

      if (*exists)
      {
         break;
      }
    }

    *is_error = false;
  }



  // +
  bool FatFS::rewind_fd_dir(int fd)
  {
    if (!is_mount_access_allowed())
    {
      return false;
    }

    auto iter = dir_fds.find(fd);
    if (iter != dir_fds.end())
    {
      iter -> second = 0;
      return true;
    }
    return false;
  }

  // +
  bool FatFS::read_fd_dir(int fd, SVMDirectoryEntryRef dir)
  {
    if (!is_mount_access_allowed())
    {
      return false;
    }

    auto iter = dir_fds.find(fd);
    if (iter != dir_fds.end())
    {
      if (iter -> second < root_dir_entries.size())
      {
        auto entries = get_root_dir_entries();
        DirEntry *entry = (*entries)[iter -> second];
        if (entry == NULL)
        {
          return false;
        }
        entry -> get_internal_entry(dir);
        (iter -> second) += 1;
        return true;
      }
    }
    return false;
  }

  // +
  bool FatFS::delete_fd(int fd)
  {
    if (!is_mount_access_allowed())
    {
      return false;
    }

    // file fd
    if (fd % 2 == 0)
    {
      std::cout << "[FatFS] unexpected delete_fd for file\n";
      return false;
    }
    // dir fd
    else
    {
      auto iter = dir_fds.find(fd);
      if (iter != dir_fds.end())
      {
        dir_fds.erase(iter);
      }
    }

    return true;
  }

  // +
  bool FatFS::init_new_fd(int fd)
  {
    if (!is_mount_access_allowed())
    {
      return false;
    }

    // file fd
    if (fd % 2 == 0)
    {
      std::cout << "[FatFS] unexpected init_new_fd for file\n";
      return false;
    }
    // dir fd
    else
    {
      dir_fds[fd] = 0;
    }

    return true;
  }

  void FatFS::get_current_working_directory(char *dst)
  {
    if (dst != NULL)
    {
      dst[0] = abs_path[0];
      dst[1] = '\0';
    }
  }

  void FatFS::debug_print_bpb_metadata()
  {
    std::cout << "-----------------METADATA---------------------\n"
      << "bytes_per_sector = " << bytes_per_sector << "\n"
      << "reserved_sectors_count = " << reserved_sectors_count << "\n"
      << "fats_count = " << (int) fats_count << "\n"
      << "root_entries_count = " << root_entries_count << "\n"
      << "sectors_per_cluster = " << (int) sectors_per_cluster << "\n"
      << "total_sectors_count = " << total_sectors_count << "\n"
      << "root_dir_sectors_count = " << root_dir_sectors_count << "\n"
      << "media_value = " << media_value << "\n"
      << "eof_value = " << eof_value << "\n"
      << "sectors_per_fat = " << sectors_per_fat << "\n"
      << "volume_serial_number = " << volume_serial_number << "\n"
      << "volume_label = " << volume_label << "\n"
      << "fat_size = " << fat_size << "\n"
      << "----------------------------------------------\n";
  }

  // n = 0 -> fat array offset
  // n = 1 -> root dir offset
  // n >= 2 -> data offset
  int FatFS::get_offset_of_cluster(int n)
  {
    int sector_index;

    // fat
    if (n == 0)
    {
      // sector index = 1
      sector_index = reserved_sectors_count;
    }
    // root dir
    else if (n == 1)
    {
      // sector index = 1 + (2 * 17) = 35
      sector_index = reserved_sectors_count + (sectors_per_fat * fats_count);
    }
    // data
    else
    {
      // sector index ~= 1 + (2 * 17) + 32 = 67
      sector_index =
        reserved_sectors_count +
        (sectors_per_fat * fats_count) + root_dir_sectors_count +
        sectors_per_cluster * (n - 2);
    }

    return sector_index * bytes_per_sector;
  }


  // +
  bool FatFS::is_fat_array_access_allowed()
  {
    return !is_fat_lock_free &&
      (fat_lock_owner_id == global_state.cur_thread_ptr -> get_thread_id());
  }

  // +
  bool FatFS::is_mount_access_allowed()
  {
    return !is_mount_lock_free &&
      (mount_lock_owner_id == global_state.cur_thread_ptr -> get_thread_id());
  }


  // +
  std::vector<uint16_t>* FatFS::get_fat_array()
  {
    if (!is_fat_array_access_allowed())
    {
      return NULL;
    }

    return &fat_arr;
  }

  // +
  FdOffset* FatFS::get_mount_fd_offset()
  {
    if (!is_mount_access_allowed())
    {
      return NULL;
    }

    return &mount_fd_offset;
  }

  // +
  std::vector<DirEntry*>* FatFS::get_root_dir_entries()
  {
    if (!is_mount_access_allowed())
    {
      return NULL;
    }

    return &root_dir_entries;
  }

  int FatFS::generate_new_fd(bool is_dir)
  {
    if (!is_mount_access_allowed())
    {
      return -1;
    }

    int fd;
    if (is_dir)
    {
      fd = FatFS::last_dir_fd;
      FatFS::last_dir_fd += 2;
    }
    else
    {
      fd = FatFS::last_file_fd;
      FatFS::last_dir_fd += 2;
    }
    return fd;
  }

  // +
  void FatFS::acquire_fat_lock(TMachineSignalStateRef mask_ptr)
  {
    TVMStatus res = OG_VMMutexAcquire(mask_ptr, fat_lock_id, VM_TIMEOUT_INFINITE);
    MachineSuspendSignals(mask_ptr);

    if (res != VM_STATUS_SUCCESS)
    {
      std::cout << "[FatFS] can't acquire_fat_lock(). Status = " << res << ".\n";
      return;
    }

    is_fat_lock_free = false;
    fat_lock_owner_id = global_state.cur_thread_ptr -> get_thread_id();
  }

  // +
  void FatFS::release_fat_lock(TMachineSignalStateRef mask_ptr)
  {
    if (is_fat_array_access_allowed())
    {
      is_fat_lock_free = true;
      fat_lock_owner_id = 0;

      TVMStatus res = OG_VMMutexRelease(mask_ptr, fat_lock_id);
      MachineSuspendSignals(mask_ptr);

      if (res != VM_STATUS_SUCCESS)
      {
        std::cout << "[FatFS] can't release_fat_lock(). Status = " << res << ".\n";
        return;
      }
    }
  }

  // +
  void FatFS::acquire_mount_lock(TMachineSignalStateRef mask_ptr)
  {
    TVMStatus res = OG_VMMutexAcquire(mask_ptr, mount_lock_id, VM_TIMEOUT_INFINITE);
    MachineSuspendSignals(mask_ptr);

    if (res != VM_STATUS_SUCCESS)
    {
      std::cout << "[FatFS] can't acquire_mount_lock(). Status = " << res << ".\n";
      return;
    }

    is_mount_lock_free = false;
    mount_lock_owner_id = global_state.cur_thread_ptr -> get_thread_id();
  }

  // +
  void FatFS::release_mount_lock(TMachineSignalStateRef mask_ptr)
  {
    if (is_mount_access_allowed())
    {
      is_mount_lock_free = true;
      mount_lock_owner_id = 0;

      TVMStatus res = OG_VMMutexRelease(mask_ptr, mount_lock_id);
      MachineSuspendSignals(mask_ptr);

      if (res != VM_STATUS_SUCCESS)
      {
        std::cout << "[FatFS] can't release_mount_lock(). Status = " << res << ".\n";
        return;
      }
    }
  }


  // +
  bool FatFS::init_metadata(TMachineSignalStateRef mask_ptr)
  {
    FdOffset* mount = get_mount_fd_offset();

    if (mount == NULL)
    {
      std::cout << "[FatFS] can't get mount [0]: didn't acquire lock\n";
      return false;
    }

    TVMStatus res = OG_VMFileSeek(
      mask_ptr, mount -> fd,
      0, 0, &(mount -> offset)
    );
    MachineSuspendSignals(mask_ptr);

    if ((res != VM_STATUS_SUCCESS) || (mount -> offset != 0))
    {
      std::cout
        << "[FatFS] can't init_metadata(): failed to seek to proper offset. "
        << "Status = " << res << ". "
        << "Offset (expected 0) = " << mount -> offset << ".\n";

      return false;
    }

    // TRY TO READ FIRST 512 BYTES
    int meta_length = 512;
    uint8_t metadata[meta_length];

    res = OG_VMFileRead(
      mask_ptr, mount -> fd,
      (void*) metadata, &meta_length
    );
    MachineSuspendSignals(mask_ptr);

    if ((res != VM_STATUS_SUCCESS) || (meta_length != 512))
    {
      std::cout
        << "[FatFS] can't init_metadata(): failed to read first 512 bytes. "
        << "Status = " << res << ". "
        << "Number of bytes read = " << meta_length << ".\n";
      return false;
    }

    mount -> offset += 512;
    // PROCESS FIRST 512 BYTES

    // assert is FAT
    if ((metadata[510] != 0x55) || (metadata[511] != 0xAA))
    {
      // VMPrintError(
        // "[FatFS] Improperly formated FAT volume. \
        // According to FAT specs, values of volume[510] and volume[511] must \
        // be 0x55 and 0xAA. Mismatch. Not a FAT volume provided\n");
      std::cout
        << "Improperly formated FAT volume. "
        << "According to FAT specs, values of volume[510] and volume[511] must "
        << "be 0x55 and 0xAA. Mismatch. Not a FAT volume provided\n";
      return false;
    }

    TO_INT_FROM_LITTLE_ENDIAN_BUFFER(metadata + 11, 2, (void*) &bytes_per_sector, 3);
    TO_INT_FROM_LITTLE_ENDIAN_BUFFER(metadata + 13, 1, (void*) &sectors_per_cluster, 1);
    TO_INT_FROM_LITTLE_ENDIAN_BUFFER(metadata + 14, 2, (void*) &reserved_sectors_count, 3);
    TO_INT_FROM_LITTLE_ENDIAN_BUFFER(metadata + 16, 1, (void*) &fats_count, 1);
    TO_INT_FROM_LITTLE_ENDIAN_BUFFER(metadata + 17, 2, (void*) &root_entries_count, 3);


    // TOTAL SECTORS COUNT
    uint16_t total_sec_16;
    uint32_t total_sec_32;
    TO_INT_FROM_LITTLE_ENDIAN_BUFFER(metadata + 19, 2, (void*) &total_sec_16, 3);
    TO_INT_FROM_LITTLE_ENDIAN_BUFFER(metadata + 32, 4, (void*) &total_sec_32, 5);
    if ((total_sec_16 == 0) && (total_sec_32 == 0))
    {
      // VMPrintError(
      //   "[FatFS] Improperly formated FAT volume. \
      //   Both BPB_TotSec32 or BPB_TotSec16 are 0. \
      //   Exactly one must have positive value.\n");

      std::cout
        << "[FatFS] Improperly formated FAT volume. "
        << "Both BPB_TotSec32 or BPB_TotSec16 are 0. "
        << "Exactly one must have positive value.\n";
      return false;
    }
    else if ((total_sec_16 != 0) && (total_sec_32 != 0))
    {
      // VMPrintError(
      //   "[FatFS]  Improperly formated FAT volume. \
      //   Both BPB_TotSec32 or BPB_TotSec16 are not 0. \
      //   Exactly one must have positive value.\n");

      std::cout
        << "[FatFS]  Improperly formated FAT volume. "
        << "Both BPB_TotSec32 or BPB_TotSec16 are not 0. "
        << "Exactly one must have positive value.\n";
      return false;
    }
    else
    {
      total_sectors_count = total_sec_16 > 0 ? total_sec_16 : total_sec_32;
    }

    // LEFTOVER VALUES
    TO_INT_FROM_LITTLE_ENDIAN_BUFFER(metadata + 21, 1, (void*) &media_value, 1);
    TO_INT_FROM_LITTLE_ENDIAN_BUFFER(metadata + 22, 2, (void*) &sectors_per_fat, 3);
    TO_INT_FROM_LITTLE_ENDIAN_BUFFER(metadata + 39, 4, (void*) &volume_serial_number, 5);

    eof_value = 0xff;
    eof_value = (eof_value << 8) + media_value;
    volume_label = std::string( ((char*) metadata) + 43, 11);

    fat_size = sectors_per_fat * bytes_per_sector / 2;

    root_dir_sectors_count =
      ((root_entries_count * 32) + (bytes_per_sector - 1)) / bytes_per_sector;

    // debug_print_bpb_metadata();

    return true;
  }


  // +-
  bool FatFS::init_fat_array(TMachineSignalStateRef mask_ptr)
  {

    auto fat_array = get_fat_array();
    if (fat_array == NULL)
    {
      std::cout << "[FatFS] can't get fat array: didn't acquire lock\n";
      return false;
    }

    FdOffset* mount = get_mount_fd_offset();
    if (mount == NULL)
    {
      std::cout << "[FatFS] can't get mount [5]: didn't acquire lock\n";
      return false;
    }


    int fat_offset = get_offset_of_cluster(0);
    TVMStatus res = OG_VMFileSeek(
      mask_ptr, mount -> fd,
      fat_offset, 0, &(mount -> offset)
    );
    MachineSuspendSignals(mask_ptr);

    if ((res != VM_STATUS_SUCCESS) || (mount -> offset != fat_offset))
    {
      std::cout
        << "[FatFS] can't init_fat_array(): failed to seek to proper offset. "
        << "Status = " << res << ". "
        << "Offset (expected " << fat_offset
        << ") = " << mount -> offset << ".\n";

      return false;
    }

    fat_array -> reserve(fat_size);

    int fat_buffer_len = sectors_per_fat * bytes_per_sector;
    int num_bytes_read = fat_buffer_len;
    uint8_t fat_buffer[fat_buffer_len];

    res = OG_VMFileRead(
      mask_ptr, mount -> fd,
      (void*) fat_buffer, &num_bytes_read
    );

    MachineSuspendSignals(mask_ptr);

    if ((res != VM_STATUS_SUCCESS) || (num_bytes_read != fat_buffer_len))
    {
      std::cout
        << "[FatFS] can't init_fat_array(): failed to read fat array. "
        << "Status = " << res << ". "
        << "Expected num of bytes = " << fat_buffer_len << ". "
        << "Number of bytes actually read = " << num_bytes_read << ".\n";
      return false;
    }

    mount -> offset += fat_buffer_len;

    // STORE FAT VALUES
    for (uint16_t i = 0; i < fat_size; i++)
    {
      uint16_t fat_val = 0;
      TO_INT_FROM_LITTLE_ENDIAN_BUFFER(fat_buffer + 2 * i, 2, (void*) &fat_val, 3);
      (*fat_array)[i] = fat_val;
    }

    // assert 0xfff8 0xffff
    if (((*fat_array)[0] != eof_value) || ((*fat_array)[1] != 0xffff))
    {
      std::cout
        << "Improperly formated FAT volume. "
        << "First values in fat table are expected to be "
        << "0xff<BPB_Media> and 0xffff. "
        << "Received: " << (*fat_array)[0] << " " << (*fat_array)[1] << "\n";

      return false;
    }

    return true;
  }

  /*
  -- add existing
  -- need to be able to add new dir
  -- need to bee able to delete existing dir
  */


  bool FatFS::init_root_dir(TMachineSignalStateRef mask_ptr)
  {
    FdOffset* mount = get_mount_fd_offset();
    if (mount == NULL)
    {
      std::cout << "[FatFS] can't get mount [11]: didn't acquire lock\n";
      return false;
    }

    std::vector<DirEntry*>* entries = get_root_dir_entries();
    if (entries == NULL)
    {
      std::cout << "[FatFS] can't get entries [182]: didn't acquire lock\n";
      return false;
    }

    int root_dir_offset = get_offset_of_cluster(1);
    TVMStatus res = OG_VMFileSeek(
      mask_ptr, mount -> fd,
      root_dir_offset, 0, &(mount -> offset)
    );
    MachineSuspendSignals(mask_ptr);

    if ((res != VM_STATUS_SUCCESS) || (mount -> offset != root_dir_offset))
    {
      std::cout
        << "[FatFS] can't init_root_dir(): failed to seek to proper offset. "
        << "Status = " << res << ". "
        << "Offset (expected " << root_dir_offset << ") = "
        << mount -> offset << ".\n";

      return false;
    }


    int num_of_bytes_to_read = bytes_per_sector;
    char root_dir_buffer[num_of_bytes_to_read];

    bool done = false;
    res = VM_STATUS_SUCCESS;
    for (int i = 0; !done && (i < root_dir_sectors_count); i++)
    {
      res = OG_VMFileRead(
        mask_ptr, mount -> fd,
        root_dir_buffer, &num_of_bytes_to_read
      );
      MachineSuspendSignals(mask_ptr);

      if ((res != VM_STATUS_SUCCESS) || (num_of_bytes_to_read != bytes_per_sector))
      {
        std::cout
          << "[FatFS] can't init_root_dir(): failed to read root dir. "
          << "Status = " << res << ". "
          << "Expected bytes read = " << bytes_per_sector << ". "
          << "Actually read = " << num_of_bytes_to_read << ". "
          << "Offset before reading = " << mount -> offset << ".\n";

        return false;
      }

      for (int i = 0; i < bytes_per_sector; i += 32)
      {
        // END
        if (root_dir_buffer[i] == 0x00)
        {
          root_dir_eof_offset = mount -> offset + i;
          done = true;
          break;
        }

        // checking attributes.
        char LONG_NAME = 0x0f;
        if (root_dir_buffer[i + 11] == LONG_NAME)
        {
          continue;
        }

        DirEntry *new_entry = new DirEntry(root_dir_buffer + i);
        entries -> push_back(new_entry);
      }
      mount -> offset += bytes_per_sector;
    }

    return true;
  }

  // +-
  // @TODO: delete dynamically created new Directory
  bool FatFS::init(const char *mount_name_ptr)
  {
    if (mount_name_ptr == NULL)
    {
      std::cout << "[FatFS] can't init(): mount_name_ptr is NULL\n";
      return false;
    }

    TMachineSignalState mask;
    MachineSuspendSignals(&mask);

    int fd = -1;
    mount_name = mount_name_ptr;

    TVMStatus res = OG_VMFileOpen(&mask, mount_name.c_str(), O_RDWR, 600, &fd);
    MachineSuspendSignals(&mask);

    if (res != VM_STATUS_SUCCESS)
    {
      std::cout
        << "[FatFS] can't init(): failed to open mount file. Status = "
        << res
        << "\n";

      MachineResumeSignals(&mask);
      return false;
    }

    mount_fd_offset.fd = fd;
    mount_fd_offset.offset = 0;

    res = VMMutexCreate(&mount_lock_id);
    if (res != VM_STATUS_SUCCESS)
    {
      std::cout << "[FatFS] Failed to create mount lock\n";
      MachineResumeSignals(&mask);
      return false;
    }

    res = VMMutexCreate(&fat_lock_id);
    if (res != VM_STATUS_SUCCESS)
    {
      std::cout << "[FatFS] Failed to create a fat array lock\n";
      MachineResumeSignals(&mask);
      return false;
    }
    acquire_mount_lock(&mask);
    acquire_fat_lock(&mask);


    bool success = init_metadata(&mask);
    if (!success)
    {
      std::cout << "[FatFS] can't init(): failed to init metadata\n";

      release_fat_lock(&mask);
      release_mount_lock(&mask);
      MachineResumeSignals(&mask);
      return false;
    }

    success = init_fat_array(&mask);
    if (!success)
    {
      std::cout << "[FatFS] can't init(): failed to init FAT array\n";

      release_fat_lock(&mask);
      release_mount_lock(&mask);
      MachineResumeSignals(&mask);
      return false;
    }

    success = init_root_dir(&mask);
    if (!success)
    {
      std::cout << "[FatFS] can't init(): failed to init root dir\n";

      release_fat_lock(&mask);
      release_mount_lock(&mask);
      MachineResumeSignals(&mask);
      return false;
    }

    release_fat_lock(&mask);
    release_mount_lock(&mask);
    MachineResumeSignals(&mask);
    return true;
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

    if (!global_state.fat_fs.init(mount))
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

  TVMStatus OG_VMMutexCreate(TMachineSignalStateRef mask, TVMMutexIDRef mutexref)
  {
    if (mutexref == NULL)
    {
      MachineResumeSignals(mask);
      return VM_STATUS_ERROR_INVALID_PARAMETER;
    }

    Lock *lock = new Lock();
    *mutexref = lock -> get_id();
    global_state.all_locks[*mutexref] = lock;

    MachineResumeSignals(mask);
    return VM_STATUS_SUCCESS;
  }
  TVMStatus VMMutexCreate(TVMMutexIDRef mutexref)
  {
    TMachineSignalState mask;
    MachineSuspendSignals(&mask);
    return OG_VMMutexCreate(&mask, mutexref);
  }

  TVMStatus OG_VMMutexDelete(TMachineSignalStateRef mask, TVMMutexID mutex)
  {
    Lock *lock_ptr = global_state.all_locks[mutex];

    if (lock_ptr == NULL)
    {
      MachineResumeSignals(mask);
      return VM_STATUS_ERROR_INVALID_ID;
    }

    if (!(lock_ptr -> is_free()))
    {
      MachineResumeSignals(mask);
      return VM_STATUS_ERROR_INVALID_STATE;
    }

    delete lock_ptr;
    global_state.all_locks.erase(mutex);

    MachineResumeSignals(mask);
    return VM_STATUS_SUCCESS;
  }
  TVMStatus VMMutexDelete(TVMMutexID mutex)
  {
    TMachineSignalState mask;
    MachineSuspendSignals(&mask);
    return OG_VMMutexDelete(&mask, mutex);
  }

  TVMStatus OG_VMMutexQuery(TMachineSignalStateRef mask, TVMMutexID mutex, TVMThreadIDRef ownerref)
  {
    if (ownerref == NULL)
    {
      MachineResumeSignals(mask);
      return VM_STATUS_ERROR_INVALID_PARAMETER;
    }

    Lock *lock_ptr = global_state.all_locks[mutex];

    if (lock_ptr == NULL)
    {
      MachineResumeSignals(mask);
      return VM_STATUS_ERROR_INVALID_ID;
    }

    *ownerref = lock_ptr -> is_free() ? VM_THREAD_ID_INVALID :
      lock_ptr -> get_owner_thread() -> get_thread_id();


    MachineResumeSignals(mask);
    return VM_STATUS_SUCCESS;
  }
  TVMStatus VMMutexQuery(TVMMutexID mutex, TVMThreadIDRef ownerref)
  {
    TMachineSignalState mask;
    MachineSuspendSignals(&mask);
    return OG_VMMutexQuery(&mask, mutex, ownerref);
  }

  TVMStatus OG_VMMutexAcquire(TMachineSignalStateRef mask, TVMMutexID mutex, TVMTick timeout)
  {
    Lock *lock_ptr = global_state.all_locks[mutex];

    if (lock_ptr == NULL)
    {
      MachineResumeSignals(mask);
      return VM_STATUS_ERROR_INVALID_ID;
    }

    if (!(lock_ptr -> acquire(timeout, mask)))
    {
      return VM_STATUS_FAILURE;
    }

    return VM_STATUS_SUCCESS;
  }
  TVMStatus VMMutexAcquire(TVMMutexID mutex, TVMTick timeout)
  {
    TMachineSignalState mask;
    MachineSuspendSignals(&mask);
    return OG_VMMutexAcquire(&mask, mutex, timeout);
  }

  TVMStatus OG_VMMutexRelease(TMachineSignalStateRef mask, TVMMutexID mutex)
  {
    Lock *lock_ptr = global_state.all_locks[mutex];

    if (lock_ptr == NULL)
    {
      MachineResumeSignals(mask);
      return VM_STATUS_ERROR_INVALID_ID;
    }

    if (lock_ptr -> get_owner_thread() != global_state.cur_thread_ptr)
    {
      MachineResumeSignals(mask);
      return VM_STATUS_ERROR_INVALID_STATE;
    }

    lock_ptr -> release();

    MachineResumeSignals(mask);
    return VM_STATUS_SUCCESS;
  }
  TVMStatus VMMutexRelease(TVMMutexID mutex)
  {
    TMachineSignalState mask;
    MachineSuspendSignals(&mask);
    return OG_VMMutexRelease(&mask, mutex);
  }























  // +
  TVMStatus VMDirectoryRewind(int dirdescriptor)
  {
    TMachineSignalState mask;
    MachineSuspendSignals(&mask);

    global_state.fat_fs.acquire_mount_lock(&mask);
    if (!global_state.fat_fs.rewind_fd_dir(dirdescriptor))
    {
      global_state.fat_fs.release_mount_lock(&mask);
      MachineResumeSignals(&mask);
      return VM_STATUS_FAILURE;
    }

    global_state.fat_fs.release_mount_lock(&mask);
    MachineResumeSignals(&mask);
    return VM_STATUS_SUCCESS;
  }


  // +
  TVMStatus VMDirectoryRead(int dirdescriptor, SVMDirectoryEntryRef dirent)
  {
    TMachineSignalState mask;
    MachineSuspendSignals(&mask);

    if (dirent == NULL)
    {
      std::cout << "[VMDirectoryRead] Error: dirent is NULL\n";
      MachineResumeSignals(&mask);
      return VM_STATUS_ERROR_INVALID_PARAMETER;
    }

    global_state.fat_fs.acquire_mount_lock(&mask);
    if (!global_state.fat_fs.read_fd_dir(dirdescriptor, dirent))
    {
      global_state.fat_fs.release_mount_lock(&mask);
      MachineResumeSignals(&mask);
      return VM_STATUS_FAILURE;
    }

    global_state.fat_fs.release_mount_lock(&mask);
    MachineResumeSignals(&mask);
    return VM_STATUS_SUCCESS;
  }

  // +
  TVMStatus VMDirectoryOpen(const char *dirname, int *dirdescriptor)
  {
    TMachineSignalState mask;
    MachineSuspendSignals(&mask);

    if ((dirname == NULL) || (dirdescriptor == NULL))
    {
      std::cout <<
        "[VMDirectoryOpen] Error: either dirname or dirdescriptor is NULL\n";
      MachineResumeSignals(&mask);
      return VM_STATUS_ERROR_INVALID_PARAMETER;
    }

    char newpath[128];
    char curpath[128];

    global_state.fat_fs.get_current_working_directory(curpath);

    TVMStatus res = VMFileSystemGetAbsolutePath(newpath, curpath, dirname);
    if (res != VM_STATUS_SUCCESS)
    {
      std::cout << "[VMDirectoryOpen] Error: incorrect path: " << dirname << "\n";
      MachineResumeSignals(&mask);
      return VM_STATUS_FAILURE;
    }

    if (std::string(newpath) != std::string(curpath))
    {
      std::cout
        << "[VMDirectoryOpen] Error: not supported path: " << dirname << "."
        << "Cur path: " << curpath << "  "
        << "New path: " << newpath << "  \n";
      MachineResumeSignals(&mask);
      return VM_STATUS_FAILURE;
    }

    global_state.fat_fs.acquire_mount_lock(&mask);
    int fd = global_state.fat_fs.generate_new_fd(true);
    if (fd == -1)
    {
      std::cout << "[VMDirectoryOpen] Error: unexpected generated fd = -1\n";
      global_state.fat_fs.release_mount_lock(&mask);
      MachineResumeSignals(&mask);
      return VM_STATUS_FAILURE;
    }

    if (!global_state.fat_fs.init_new_fd(fd))
    {
      std::cout << "[VMDirectoryOpen] Error: failed to init fd\n";
      global_state.fat_fs.release_mount_lock(&mask);
      MachineResumeSignals(&mask);
      return VM_STATUS_FAILURE;
    }

    *dirdescriptor = fd;

    global_state.fat_fs.release_mount_lock(&mask);

    MachineResumeSignals(&mask);
    return VM_STATUS_SUCCESS;
  }

  // +
  TVMStatus VMDirectoryClose(int dirdescriptor)
  {
    TMachineSignalState mask;
    MachineSuspendSignals(&mask);

    global_state.fat_fs.acquire_mount_lock(&mask);

    if (!global_state.fat_fs.delete_fd(dirdescriptor))
    {
      std::cout << "[VMDirectoryClose] Error: unexpected error on deletion \n";
      global_state.fat_fs.release_mount_lock(&mask);
      MachineResumeSignals(&mask);
      return VM_STATUS_FAILURE;
    }

    global_state.fat_fs.release_mount_lock(&mask);

    MachineResumeSignals(&mask);
    return VM_STATUS_SUCCESS;
  }

  // +
  TVMStatus VMDirectoryCurrent(char *abspath)
  {
    TMachineSignalState mask;
    MachineSuspendSignals(&mask);

    if (abspath == NULL)
    {
      MachineResumeSignals(&mask);
      std::cout << "[VMDirectoryCurrent] Error: abspath is NULL\n";
      return VM_STATUS_ERROR_INVALID_PARAMETER;
    }

    global_state.fat_fs.get_current_working_directory(abspath);

    MachineResumeSignals(&mask);
    return VM_STATUS_SUCCESS;
  }

  // +
  TVMStatus VMDirectoryChange(const char *path)
  {
    TMachineSignalState mask;
    MachineSuspendSignals(&mask);

    if (path == NULL)
    {
      std::cout << "[VMDirectoryChange] Error: path is NULL\n";
      MachineResumeSignals(&mask);
      return VM_STATUS_ERROR_INVALID_PARAMETER;
    }

    char newpath[128];
    char curpath[128];

    global_state.fat_fs.get_current_working_directory(curpath);

    TVMStatus res = VMFileSystemGetAbsolutePath(newpath, curpath, path);
    if ((res != VM_STATUS_SUCCESS) || (std::string(newpath) != std::string(curpath)))
    {
      std::cout
        << "[VMDirectoryChange] Error: not supported path: " << path << "."
        << "Cur path: " << curpath << "  "
        << "New path: " << newpath << "  \n";

      MachineResumeSignals(&mask);
      return VM_STATUS_FAILURE;
    }

    MachineResumeSignals(&mask);
    return VM_STATUS_SUCCESS;
  }

  TVMStatus VMDirectoryCreate(const char *dirname)
  {
    return VM_STATUS_FAILURE;
  }

  TVMStatus VMDirectoryUnlink(const char *path)
  {
    return VM_STATUS_FAILURE;
  }




  // +-
  TVMStatus VMFileOpen(const char *filename, int flags, int mode, int *filedescriptor)
  {
    TMachineSignalState mask;
    MachineSuspendSignals(&mask);

    if ((filedescriptor == NULL) || (filename == NULL))
    {
      MachineResumeSignals(&mask);
      return VM_STATUS_ERROR_INVALID_PARAMETER;
    }

    TVMStatus res = VMFileSystemValidPathName(filename);
    if (res != VM_STATUS_SUCCESS)
    {
      std::cout << "[VMFileOpen] Error: invalid filename: " << filename << ".\n";

      MachineResumeSignals(&mask);
      return VM_STATUS_FAILURE;
    }

    char absolute_path[128];
    char cur_work_dir[128];
    global_state.fat_fs.get_current_working_directory(cur_work_dir);

    // @TODO: /one/../tmp.txt
    res = VMFileSystemGetAbsolutePath(
      absolute_path,
      cur_work_dir,
      filename
    );

    if (res != VM_STATUS_SUCCESS)
    {
      std::cout
        << "[VMFileOpen] Error: failed to get system absolute path for '"
        << filename << "'. "
        << "Res: " << res << ". "
        << "Cur path: " << cur_work_dir << " \n";

      MachineResumeSignals(&mask);
      return VM_STATUS_FAILURE;
    }

    char open_fname[128];
    char open_path[128];

    res = VMFileSystemDirectoryFromFullPath(open_path, absolute_path);
    if (res != VM_STATUS_SUCCESS)
    {
      std::cout
        << "[VMFileOpen] Error: failed to get dir from system absolute path. "
        << "Filename = '" << filename << " "
        << "Res = " << res << " "
        << "Abs path = " << absolute_path << " \n";

      MachineResumeSignals(&mask);
      return VM_STATUS_FAILURE;
    }

    res = VMFileSystemFileFromFullPath(open_fname, absolute_path);
    if (res != VM_STATUS_SUCCESS)
    {
      std::cout
        << "[VMFileOpen] Error: failed to get fname from system absolute path. "
        << "Filename = '" << filename << " "
        << "Res = " << res << " "
        << "Abs path = " << absolute_path << " \n";
      MachineResumeSignals(&mask);
      return VM_STATUS_FAILURE;
    }

    if ((res != VM_STATUS_SUCCESS) || (std::string(open_path) != std::string(cur_work_dir)))
    {
      std::cout
        << "[VMFileOpen] Error: not supported path: " << filename << " "
        << "Cur path: " << cur_work_dir << "  "
        << "New path: " << open_path << "  \n";
        MachineResumeSignals(&mask);
        return VM_STATUS_FAILURE;
    }


    bool can_read = false;
    bool can_write = false;
    bool file_must_already_exist = true;
    bool to_clear_file = false;

    bool valid_flags = PARSE_FLAGS(
      flags, &can_read, &can_write, &file_must_already_exist, &to_clear_file);

    if (!valid_flags)
    {
      std::cout
        << "[VMFileOpen] Error during parsing flags. "
        << "File name = '" << open_fname << "' "
        << "Flags:\n" <<
          "\tcan_read = " << can_read << "\n" <<
          "\tcan_write = " << can_write << "\n" <<
          "\tfile_must_already_exist = " << file_must_already_exist << "\n" <<
          "\tto_clear_file = " << to_clear_file << "\n";

      MachineResumeSignals(&mask);
      return VM_STATUS_FAILURE;
    }

    std::cout << "[VMFileOpen] Flags: " << flags << "\n"
      "\tcan_read = " << can_read << "\n" <<
      "\tcan_write = " << can_write << "\n" <<
      "\tfile_must_already_exist = " << file_must_already_exist << "\n" <<
      "\tto_clear_file = " << to_clear_file << "\n"
      "File: " << open_fname << "\n";


    global_state.fat_fs.acquire_mount_lock(&mask);

    std::string short_name = LONG_TO_SHORT_NAME(open_fname);


    bool file_already_exists;
    bool is_error = false;

    global_state.fat_fs.file_entry_exists(short_name, &file_already_exists, &is_error);

    if (is_error)
    {
      std::cout
        << "[VMFileOpen] Error during reading cur dir files. No lock acquired/\n";

      global_state.fat_fs.release_mount_lock(&mask);
      MachineResumeSignals(&mask);
      return VM_STATUS_FAILURE;
    }

    /*
    1. file doesn't exist and we don't create -> return VM_STATUS_FAILURE
    2. file doesn't exist and we're supposed to create -> CASE_create_new_file_entry
       file exists:
    3. - and need to be cleared -> CASE_clear_file_entry()
    4.
    5. init file entry fd & store flag values (read/write)

    SUMMARY:
    - IMPLEMENT CREATION OF FILE ENTRY FD
    - IMPLEMENT CHECKING IF FILE ALREADY EXISTS IN CWD ---------- done
    - IMPLEMENT NEW FILE CREATION ------------ doing
    - IMPLEMENT CLEARING EXISTING FILES
    */

    if (!file_already_exists && (file_must_already_exist))
    {
      std::cout
        << "[VMFileOpen] Can't open: file'" << short_name << "' doesn't exist. "
        << "Flags don't specify O_CREAT flag\n";

      global_state.fat_fs.release_mount_lock(&mask);
      MachineResumeSignals(&mask);
      return VM_STATUS_FAILURE;
    }

    global_state.fat_fs.acquire_fat_lock(&mask);

    int cluster_number = -1;
    if (!global_state.fat_fs.allocate_new_cluster(&mask, &cluster_number))
    {
      std::cout
        << "[VMFileOpen] Failed to allocate a new cluster id in FAT array. "
        << "Possibly, didn't acquire fat lock?\n";

      global_state.fat_fs.release_fat_lock(&mask);
      global_state.fat_fs.release_mount_lock(&mask);
      return VM_STATUS_FAILURE;
    }
    global_state.fat_fs.release_fat_lock(&mask);

    std::cout << "CLUSTER ID = " << cluster_number << "\n";

    global_state.fat_fs.release_mount_lock(&mask);

    MachineResumeSignals(&mask);
    return VM_STATUS_SUCCESS;
  }

  // -
  TVMStatus VMFileClose(int filedescriptor)
  {
    TMachineSignalState mask;
    MachineSuspendSignals(&mask);

    if ((filedescriptor >= 0) && (filedescriptor <= 2))
    {
      return OG_VMFileClose(&mask, filedescriptor);
    }

    std::cout << ">> [VMFileClose] Error. File Descriptor: " << filedescriptor << "\n";

    MachineResumeSignals(&mask);
    return VM_STATUS_FAILURE;
  }

  // -
  TVMStatus VMFileRead(int filedescriptor, void *data, int *length)
  {
    TMachineSignalState mask;
    MachineSuspendSignals(&mask);

    if ((filedescriptor >= 0) && (filedescriptor <= 2))
    {
      return OG_VMFileRead(&mask, filedescriptor, data, length);
    }

    std::cout << ">> [VMFileRead] Error. File Descriptor: " << filedescriptor << "\n";

    MachineResumeSignals(&mask);
    return VM_STATUS_FAILURE;
  }

  // -
  TVMStatus VMFileWrite(int filedescriptor, void *data, int *length)
  {
    TMachineSignalState mask;
    MachineSuspendSignals(&mask);

    if ((filedescriptor >= 0) && (filedescriptor <= 2))
    {
      return OG_VMFileWrite(&mask, filedescriptor, data, length);
    }

    std::cout << ">> [VMFileWrite] Error. File Descriptor: " << filedescriptor << "\n";

    MachineResumeSignals(&mask);
    return VM_STATUS_FAILURE;
  }

  // -
  TVMStatus VMFileSeek(int filedescriptor, int offset, int whence, int *newoffset)
  {
    TMachineSignalState mask;
    MachineSuspendSignals(&mask);

    if ((filedescriptor >= 0) && (filedescriptor <= 2))
    {
      return OG_VMFileSeek(&mask, filedescriptor, offset, whence, newoffset);
    }

    std::cout << ">> [VMFileSeek] Error. File Descriptor: " << filedescriptor << "\n";

    MachineResumeSignals(&mask);
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
}
