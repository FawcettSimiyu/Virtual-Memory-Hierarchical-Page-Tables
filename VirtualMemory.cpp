#include "VirtualMemory.h"
#include "PhysicalMemory.h"

#define FAILURE 0
#define SUCCESS 1

#define FRAME_SIZE PAGE_SIZE
#define ROOT_TABLE_FRAME_NUMBER 0
#define ROOT_TABLE_LEVEL 0

#define INIT_ACCUMULATED_P 0

// Invalid table frame index
#define INVALID_TABLE_FRAME 0

// Default root table parent
#define ROOT_TABLE_PARENT_INFO {0, 0}

struct PTEInfo;

// ---- helper functions definitions ----
int abs(int x);
void attachChildToFather(word_t currentFrame, const word_t &nextFrame, const uint64_t &offset);
int getCyclicDist(uint64_t pageSwappedIn, uint64_t p);
bool isVirtualAddressValid(uint64_t virtualAddress);
void initializeFrameTable(word_t frameNumber);
uint64_t extractPageTableIndex(uint64_t virtualAddress, uint64_t level);
bool isValidTableFrameIndex(word_t frameIndex);
bool isLeaf(uint64_t level);
void detachChildFromParent(word_t parentFrame, int childIndex);
bool isValidFrame(word_t frame);
void handleEviction(word_t &victimFrame, uint64_t pageNumber);
void handlePageFault(word_t currentFrame, word_t& nextFrame, uint64_t p_level, uint64_t level, uint64_t pageNumber);
uint64_t mapVirtualAddressToPhysicalAddress(uint64_t virtualAddress);

// ---- Header file functions implementation ----

void VMinitialize() {
    initializeFrameTable(ROOT_TABLE_FRAME_NUMBER);
}

int VMread(uint64_t virtualAddress, word_t* value) {
    if (!isVirtualAddressValid(virtualAddress)) return FAILURE;
    uint64_t physicalAddress = mapVirtualAddressToPhysicalAddress(virtualAddress);
    PMread(physicalAddress, value);
    return SUCCESS;
}

int VMwrite(uint64_t virtualAddress, word_t value) {
    if (!isVirtualAddressValid(virtualAddress)) return FAILURE;
    uint64_t physicalAddress = mapVirtualAddressToPhysicalAddress(virtualAddress);
    PMwrite(physicalAddress, value);
    return SUCCESS;
}

// ---- helper functions and structs implementation ----

int abs(int x) {
    return x < 0 ? -x : x;
}

struct PTEInfo {
    word_t frame;
    int offset;
};

uint64_t getPhysicalAddress(int frame, uint64_t offset) {
    return frame * FRAME_SIZE + offset;
}

bool isVirtualAddressValid(uint64_t virtualAddress) {
    return virtualAddress < VIRTUAL_MEMORY_SIZE;
}

void initializeFrameTable(word_t frameNumber) {
    uint64_t baseAddress = frameNumber * FRAME_SIZE;
    uint64_t endAddress = baseAddress + FRAME_SIZE;
    for (uint64_t physicalAddress = baseAddress; physicalAddress < endAddress; physicalAddress++) {
        PMwrite(physicalAddress, INVALID_TABLE_FRAME);
    }
}

uint64_t extractPageTableIndex(uint64_t virtualAddress, uint64_t level) {
    int shiftAmount = OFFSET_WIDTH * (TABLES_DEPTH - level);
    uint64_t mask = (1LL << OFFSET_WIDTH) - 1;
    return (virtualAddress >> shiftAmount) & mask;
}

bool isValidTableFrameIndex(word_t frameIndex) {
    return frameIndex != 0 && frameIndex < NUM_FRAMES;
}

void findVictimFrame(const word_t DFSRoot, int level, word_t accumulatedP, int &maxCyclicDist,
                     const word_t pageSwappedIn, word_t &argmaxPageFrame, word_t &pageToEvict,
                     const PTEInfo parentInfo, PTEInfo& evictedPageParentInfo) {
    if (level == TABLES_DEPTH) {
        int cyclicDist = getCyclicDist(pageSwappedIn, accumulatedP);
        if (cyclicDist > maxCyclicDist) {
            maxCyclicDist = cyclicDist;
            argmaxPageFrame = DFSRoot;
            pageToEvict = accumulatedP;
            evictedPageParentInfo = parentInfo;
        }
        return;
    }
    word_t childFrame = INVALID_TABLE_FRAME;
    for (int offset = 0; offset < PAGE_SIZE; offset++) {
        PMread(DFSRoot * PAGE_SIZE + offset, &childFrame);
        if (childFrame != INVALID_TABLE_FRAME) {
            findVictimFrame(childFrame, level + 1,
                            (accumulatedP << OFFSET_WIDTH) | offset, maxCyclicDist,
                            pageSwappedIn, argmaxPageFrame, pageToEvict,
                            {DFSRoot, offset}, evictedPageParentInfo);
        }
    }
                     }

                     int getCyclicDist(uint64_t pageSwappedIn, uint64_t p) {
                         int dist = abs((int)(pageSwappedIn - p));
                         int upperDist = NUM_PAGES - dist;
                         return upperDist < dist ? upperDist : dist;
                     }

                     bool isLeaf(uint64_t level) {
                         return level == TABLES_DEPTH - 1;
                     }

                     void detachChildFromParent(word_t parentFrame, int childIndex) {
                         PMwrite(getPhysicalAddress(parentFrame, childIndex), INVALID_TABLE_FRAME);
                     }

                     bool isValidFrame(word_t frame) {
                         return frame > INVALID_TABLE_FRAME && frame < NUM_FRAMES;
                     }

                     void getFrameWithoutEvictionDFS(const word_t parentToNotEmpty, word_t DFSRoot, int &maxFrame,
                                                     word_t &emptyFrame, int level, PTEInfo parentInfo, PTEInfo &evictedPageParentInfo) {
                         if (level == TABLES_DEPTH) return;
                         bool isEmptyTable = true;
                         word_t childFrame = INVALID_TABLE_FRAME;
                         for (int offset = 0; offset < PAGE_SIZE; ++offset) {
                             PMread(getPhysicalAddress(DFSRoot, offset), &childFrame);
                             if (childFrame > maxFrame) maxFrame = childFrame;
                             if (childFrame != INVALID_TABLE_FRAME) {
                                 isEmptyTable = false;
                                 getFrameWithoutEvictionDFS(parentToNotEmpty, childFrame, maxFrame,
                                                            emptyFrame, level + 1,
                                                            {DFSRoot, offset}, evictedPageParentInfo);
                             }
                         }
                         if (isEmptyTable && (DFSRoot != parentToNotEmpty)) {
                             emptyFrame = DFSRoot;
                             evictedPageParentInfo = parentInfo;
                         }
                                                     }

                                                     void handleFrameWithoutEviction(const word_t currentFrame, word_t& nextFrame) {
                                                         word_t emptyFrame = 0;
                                                         int maxFrame = 0;
                                                         PTEInfo evictedPageParentInfo = ROOT_TABLE_PARENT_INFO;
                                                         getFrameWithoutEvictionDFS(currentFrame, INVALID_TABLE_FRAME, maxFrame,
                                                                                    emptyFrame, ROOT_TABLE_LEVEL, ROOT_TABLE_PARENT_INFO,
                                                                                    evictedPageParentInfo);

                                                         if (emptyFrame != INVALID_TABLE_FRAME) {
                                                             nextFrame = emptyFrame;
                                                             detachChildFromParent(evictedPageParentInfo.frame, evictedPageParentInfo.offset);
                                                         } else if (isValidFrame(maxFrame + 1)) {
                                                             nextFrame = maxFrame + 1;
                                                         }
                                                     }

                                                     void handleEviction(word_t &victimFrame, uint64_t pageNumber) {
                                                         int maxDist = 0;
                                                         word_t pageToEvict = 0;
                                                         PTEInfo parentOfEvicted = ROOT_TABLE_PARENT_INFO;
                                                         findVictimFrame(INVALID_TABLE_FRAME, ROOT_TABLE_LEVEL, INIT_ACCUMULATED_P, maxDist,
                                                                         (word_t)pageNumber, victimFrame, pageToEvict,
                                                                         ROOT_TABLE_PARENT_INFO, parentOfEvicted);

                                                         PMevict(victimFrame, pageToEvict);
                                                         detachChildFromParent(parentOfEvicted.frame, parentOfEvicted.offset);
                                                     }

                                                     void handlePageFault(word_t currentFrame, word_t& nextFrame, uint64_t p_level, uint64_t level, uint64_t pageNumber) {
                                                         handleFrameWithoutEviction(currentFrame, nextFrame);
                                                         if (nextFrame == INVALID_TABLE_FRAME) {
                                                             handleEviction(nextFrame, pageNumber);
                                                         }
                                                         attachChildToFather(currentFrame, nextFrame, p_level);
                                                         if (isLeaf(level)) {
                                                             PMrestore(nextFrame, pageNumber);
                                                         } else {
                                                             initializeFrameTable(nextFrame);
                                                         }
                                                     }

                                                     void attachChildToFather(word_t currentFrame, const word_t &nextFrame, const uint64_t &offset) {
                                                         PMwrite(currentFrame * FRAME_SIZE + offset, nextFrame);
                                                     }

                                                     uint64_t mapVirtualAddressToPhysicalAddress(uint64_t virtualAddress) {
                                                         uint64_t pageNumber = virtualAddress >> OFFSET_WIDTH;
                                                         word_t currentFrame = ROOT_TABLE_FRAME_NUMBER;
                                                         for (int level = 0; level < TABLES_DEPTH; level++) {
                                                             uint64_t p_level = extractPageTableIndex(virtualAddress, level);
                                                             word_t nextFrame;
                                                             PMread(getPhysicalAddress(currentFrame, p_level), &nextFrame);
                                                             if (!isValidTableFrameIndex(nextFrame)) {
                                                                 handlePageFault(currentFrame, nextFrame, p_level, level, pageNumber);
                                                             }
                                                             currentFrame = nextFrame;
                                                         }
                                                         auto mask = (1LL << OFFSET_WIDTH) - 1;
                                                         auto offset = virtualAddress & mask;
                                                         return currentFrame * FRAME_SIZE + offset;
                                                     }
