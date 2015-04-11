//
// File:        rm_filehandle.cc
// Description: RM_FileHandle class implementation
// Authors:     Aditya Bhandari (adityasb@stanford.edu)
//

#include "rm.h"
#include <cstring>
using namespace std;

// Default constructor
RM_FileHandle::RM_FileHandle() {
    // Set open flag to false
    isOpen = FALSE;

    // Set header modified flag to false
    headerModified = FALSE;

    // Initialize the file header
    fileHeader.numberPages = 0;
    fileHeader.firstFreePage = NO_FREE_PAGE;
}

// Destructor
RM_FileHandle::~RM_FileHandle() {
    // Delete the PF file handle
    delete &pfFH;

    // Delete the file header
    delete &fileHeader;
}

// Copy constructor
RM_FileHandle::RM_FileHandle(const RM_FileHandle &fileHandle) {
    // Copy the PF file handle and open flag
    this->pfFH = fileHandle.pfFH;
    this->isOpen = fileHandle.isOpen;
}

// Overload =
RM_FileHandle& RM_FileHandle::operator=(const RM_FileHandle &fileHandle) {
    // Check for self-assignment
    if (this != &fileHandle) {
        // Copy the PF file handle and open flag
        this->pfFH = fileHandle.pfFH;
        this->isOpen = fileHandle.isOpen;
    }

    // Return a reference to this
    return (*this);
}

// Method: GetRec(const RID &rid, RM_Record &rec) const
// Given a RID, return the record
/* Steps:
    1) Check if the file is open
    2) Get the page and slot numbers for the required record
    3) Open the PF PageHandle for the page
    4) Get the data from the page
    5) Calculate the record offset using the slot number
    6) Copy the record from the page to rec
        - Create a new record
        - Copy the data to the new record
        - Set the RID of the new record
        - Point rec to the new record
    7) Unpin the page
*/
RC RM_FileHandle::GetRec(const RID &rid, RM_Record &rec) const {
    // Check if the file is open
    if (!isOpen) {
        return RM_FILE_CLOSED;
    }

    // Declare an integer for the return code
    int rc;

    // Get the page number for the required record
    PageNum pageNumber;
    if ((rc = rid.GetPageNum(pageNumber))) {
        // Return the error from the RID class
        return rc;
    }

    // Get the slot number for the required record
    SlotNum slotNumber;
    if ((rc = rid.GetSlotNum(slotNumber))) {
        // Return the error from the RID class
        return rc;
    }

    // Open the corresponding PF page handle
    PF_PageHandle pfPH;
    if ((rc = pfFH.GetThisPage(pageNumber, pfPH))) {
        // Return the error from the PF FileHandle
        return rc;
    }

    // Get the data from the page
    char* pData;
    if ((rc = pfPH.GetData(pData))) {
        // Return the error from the PF PageHandle
        return rc;
    }

    // Check whether the slot number is valid
    int numberRecords = fileHeader.numberRecordsOnPage;
    if (slotNumber < 1 || slotNumber > numberRecords) {
        // Return error
        return RM_INVALID_SLOT_NUMBER;
    }

    // Get the record offset
    int recordOffset = getRecordOffset(slotNumber);

    // Get the record by using the record offset
    RM_Record newRecord;

    // Set the data in the new record
    char* newPData;
    if ((rc = newRecord.GetData(newPData))) {
        // Return the error from the RM Record
        return rc;
    }
    char* data = pData + recordOffset;
    memcpy(newPData, data, fileHeader.recordSize);

    // Set the RID of the new record
    RID newRid;
    if ((rc = newRecord.GetRid(newRid))) {
        // Return the error from the RM Record
        return rc;
    }
    newRid = rid;

    // Set valid flag of record to true
    newRecord.isValid = TRUE;

    // Set rec to point to the new record
    rec = newRecord;

    // Unpin the page
    if ((rc = pfFH.UnpinPage(pageNumber))) {
        // Return the error from the PF FileHandle
        return rc;
    }

    // Return OK
    return OK_RC;
}


// Method: InsertRec(const char *pData, RID &rid)
// Insert a new record
/* Steps:
    1) Check if the file is open
    2) Get the first free page from the file header
    3) If no free page (first free page number is NO_FREE_PAGE)
        - Allocate a new page
        - Initialize the page header and bitmap
        - Increment the number of pages in the file header
    4) Get the first free slot from the bitmap
    5) Calculate the record offset
    6) Insert the record in the free slot
        - Copy the data to the free slot
        - Update the bitmap on the page
    7) If the page becomes full (check bitmap)
        - Get the next free page number
        - Update the first free page number in the file header
        - Set the next free page on the page header to NO_FREE_PAGE
    8) Mark the page as dirty
    9) Unpin the page
*/
RC RM_FileHandle::InsertRec(const char *pData, RID &rid) {
    // Check if the file is open
    if (!isOpen) {
        return RM_FILE_CLOSED;
    }

    // Declare an integer for the return code
    int rc;

    // Calculate the bitmap size
    int numberRecords = fileHeader.numberRecordsOnPage;
    int bitmapSize = numberRecords/8;
    if (numberRecords%8 != 0) bitmapSize++;

    // Get the first free page number
    PageNum freePageNumber = fileHeader.firstFreePage;
    PF_PageHandle pfPH;

    // If there is no free page
    if (freePageNumber == NO_FREE_PAGE) {
        // Allocate a new page in the file
        if ((rc = pfFH.AllocatePage(pfPH))) {
            // Return the error from the PF FileHandle
            return rc;
        }

        // Set the page header and bitmap for the new page
        char* pHData;
        if ((rc = pfPH.GetData(pHData))) {
            // Return the error from the PF PageHandle
            return rc;
        }

        // Initialize the page header
        RM_PageHeader* pageHeader;
        pageHeader->nextPage = NO_FREE_PAGE;

        // Initialize the bitmap to all 0s
        char* bitmap = new char[bitmapSize];
        for (int i=0; i<bitmapSize; i++) {
            bitmap[i] = 0x00;
        }

        // Copy the page header and bitmap to pHData
        memcpy(pHData, pageHeader, sizeof(RM_PageHeader));
        char* offset = pHData + sizeof(RM_PageHeader);
        memcpy(offset, bitmap, bitmapSize);

        // Set freePageNumber to this page
        if ((rc = pfPH.GetPageNum(freePageNumber))) {
            // Return the error from the PF PageHandle
            return rc;
        }

        // Increment the number of pages in the file header
        fileHeader.numberPages++;
        headerModified = TRUE;
    }

    // Get the page data
    if ((rc = pfFH.GetThisPage(freePageNumber, pfPH))) {
        // Return the error from the PF FileHandle
        return rc;
    }
    char* freePageData;
    if ((rc = pfPH.GetData(freePageData))) {
        // Return the error from the PF PageHandle
        return rc;
    }

    // Get the first free slot from the bitmap
    char* bitmap = freePageData + sizeof(RM_PageHeader);
    int freeSlotNumber = getFirstZeroBit(bitmap, bitmapSize);

    // Calculate the record offset
    int recordOffset = getRecordOffset(freeSlotNumber);

    // Copy the data to the free slot
    memcpy(freePageData+recordOffset, pData, fileHeader.recordSize);

    // Update the bitmap
    if ((rc = SetBit(freeSlotNumber, bitmap))) {
        // Return the error from the set bit method
        return rc;
    }

    // Check if the page has become full
    if (isBitmapFull(bitmap, bitmapSize)) {
        // Get the next free page number
        RM_PageHeader* pH = (RM_PageHeader*) freePageData;
        int nextFreePageNumber = pH->nextPage;

        // Update the first free page in the file header
        fileHeader.firstFreePage = nextFreePageNumber;
        headerModified = TRUE;

        // Set the next free page on the current page header
        pH->nextPage = NO_FREE_PAGE;
    }

    // Mark the page as dirty
    if ((rc = pfFH.MarkDirty(freePageNumber))) {
        // Return the error from the PF FileHandle
        return rc;
    }

    // Unpin the page
    if ((rc = pfFH.UnpinPage(freePageNumber))) {
        // Return the error from the PF FileHandle
        return rc;
    }

    // Return OK
    return OK_RC;
}


// Method: DeleteRec(const RID &rid)
// Delete a record
/* Steps:
    1) Check if the file is open
    2) Get the page number and slot number from the RID
        - Check if the slot number is valid
    3) Calculate the record offset
    4) Check if the page is full (check bitmap)
    4) Change the bit in the bitmap to 0
    5) If the page was previously full
        - Set the next free page in the page header to the first free page in the file header
        - Set the first free page in the file header to this page
    6) Mark the page as dirty
    7) Unpin the page
    8) If the page becomes empty (check bitmap)
        - Delete the bitmap
        - Dispose the page using the PF FileHandle
        - Decrement the number of pages in the file header
*/
RC RM_FileHandle::DeleteRec(const RID &rid) {
    // Check if the file is open
    if (!isOpen) {
        return RM_FILE_CLOSED;
    }

    // Declare an integer for the return code
    int rc;

    // Get the page number and slot number from the RID
    int pageNumber, slotNumber;
    if ((rc = rid.GetPageNum(pageNumber))) {
        // Return the error from the RID
        return rc;
    }
    if ((rc = rid.GetSlotNum(slotNumber))) {
        // Return the error from the RID
        return rc;
    }

    // Check whether the slot number is valid
    int numberRecords = fileHeader.numberRecordsOnPage;
    if (slotNumber < 1 || slotNumber > numberRecords) {
        // Return error
        return RM_INVALID_SLOT_NUMBER;
    }

    // Get the page data
    PF_PageHandle pfPH;
    char* pageData;
    if ((rc = pfFH.GetThisPage(pageNumber, pfPH))) {
        // Return the error from the PF FileHandle
        return rc;
    }
    if ((rc = pfPH.GetData(pageData))) {
        // Return the error from the PF PageHandle
        return rc;
    }

    // Calculate the record offset
    int recordOffset = getRecordOffset(slotNumber);

    // Check if the page is full
    int bitmapSize = numberRecords/8;
    if (numberRecords%8 != 0) bitmapSize++;
    char* bitmap = pageData + sizeof(RM_PageHeader);
    bool pageFull = isBitmapFull(bitmap, bitmapSize);

    // Change the corresponding bit in the bitmap to 0
    if ((rc = UnsetBit(slotNumber, bitmap))) {
        // Return the error from the unset bit method
        return rc;
    }

    // If the page was previously full, add it now to the free list
    if (pageFull) {
        // Get the original first free page
        PageNum firstFreePage = fileHeader.firstFreePage;

        // Set the next free page in the page header to this
        RM_PageHeader* pH = (RM_PageHeader*) pageData;
        pH->nextPage = firstFreePage;

        // Set the first free page in the file header to this page
        fileHeader.firstFreePage = pageNumber;
        headerModified = TRUE;
    }

    // Mark the page as dirty
    if ((rc = pfFH.MarkDirty(pageNumber))) {
        // Return the error from the PF FileHandle
        return rc;
    }

    // Unpin the page
    if ((rc = pfFH.UnpinPage(pageNumber))) {
        // Return the error from the PF FileHandle
        return rc;
    }

    // If the page becomes empty, dispose the page
    if (isBitmapEmpty(bitmap, bitmapSize)) {
        // Delete the bitmap
        delete[] bitmap;

        // Dispose the page using the PF FileHandle
        if ((rc = pfFH.DisposePage(pageNumber))) {
            // Return the error from the PF FileHandle
            return rc;
        }

        // Decrement the number of pages in the file header
        fileHeader.numberPages--;
        headerModified = TRUE;
    }

    // Return OK
    return OK_RC;
}


// Method: UpdateRec(const RM_Record &rec)
// Update a record
/* Steps:
    1) Check if the file is open
    2) Get the RID for the record
    3) Get the page number and slot number for the record
        - Check whether the slot number is valid
    4) Open the PF PageHandle for the page
    5) Calculate the record offset
    6) Update the record on the page
        - Copy the data from the record to the data on page
    7) Mark the page as dirty
    8) Unpin the page
*/
RC RM_FileHandle::UpdateRec(const RM_Record &rec) {
    // Check if the file is open
    if (!isOpen) {
        return RM_FILE_CLOSED;
    }

    // Declare an integer for the return code
    int rc;

    // Get the RID for the record
    RID rid;
    if ((rc = rec.GetRid(rid))) {
        // Return the error from the RM Record
        return rc;
    }

    // Get the page number for the required record
    PageNum pageNumber;
    if (rc = rid.GetPageNum(pageNumber)) {
        // Return the error from the RID class
        return rc;
    }

    // Get the slot number for the required record
    SlotNum slotNumber;
    if (rc = rid.GetSlotNum(slotNumber)) {
        // Return the error from the RID class
        return rc;
    }

    // Check whether the slot number is valid
    int numberRecords = fileHeader.numberRecordsOnPage;
    if (slotNumber < 1 || slotNumber > numberRecords) {
        // Return an error
        return RM_INVALID_SLOT_NUMBER;
    }

    // Open the corresponding PF page handle
    PF_PageHandle pfPH;
    if (rc = pfFH.GetThisPage(pageNumber, pfPH)) {
        // Return the error from the PF FileHandle
        return rc;
    }

    // Get the data from the page
    char* pData;
    if (rc = pfPH.GetData(pData)) {
        // Return the error from the PF PageHandle
        return rc;
    }

    // Get the record offset
    int recordOffset = getRecordOffset(slotNumber);

    // Get the record data
    char* recData;
    if ((rc = rec.GetData(recData))) {
        // Return the error from the RM Record
        return rc;
    }

    // Update the data in the file to the record data
    memcpy(pData, recData, fileHeader.recordSize);

    // Mark the page as dirty
    if ((rc = pfFH.MarkDirty(pageNumber))) {
        // Return the error from the PF FileHandle
        return rc;
    }

    // Unpin the page
    if ((rc = pfFH.UnpinPage(pageNumber))) {
        // Return the error from the PF FileHandle
        return rc;
    }

    // Return OK
    return OK_RC;
}


// Method: ForcePages(PageNum pageNum = ALL_PAGES)
// Forces a page (along with any contents stored in this class)
// from the buffer pool to disk.  Default value forces all pages.
RC RM_FileHandle::ForcePages(PageNum pageNum) {
    // Declare an integer for the return code
    int rc;

    // Force the pages using the PF FileHandle
    if (rc = pfFH.ForcePages(pageNum)) {
        // Return the error from the PF FileHandle
        return rc;
    }

    // Return OK
    return OK_RC;
}


// Method: getRecordOffset(int slotNumber)
// Calculate the record offset given the slot number
int RM_FileHandle::getRecordOffset(int slotNumber) const {
    // Get the bitmap size
    int numberRecords = fileHeader.numberRecordsOnPage;
    int bitmapSize = numberRecords/8;
    if (numberRecords % 8 != 0) bitmapSize++;

    // Get the record offset
    int recordSize = fileHeader.recordSize;
    int recordOffset = sizeof(RM_PageHeader) + bitmapSize + (slotNumber-1)*recordSize;

    return recordOffset;
}

// Method: SetBit(int bitNumer, char* bitmap)
// Set bit in the bitmap to 1
RC RM_FileHandle::SetBit(int bitNumer, char* bitmap) {

}

// Method: UnsetBit(int bitNumber, char* bitmap)
// Set bit in the bitmap to 0
RC RM_FileHandle::UnsetBit(int bitNumber, char* bitmap) {

}

// Method: int getFirstZeroBit(char* bitmap, int bitmapSize)
// Get the first 0 bit in the bitmap
int RM_FileHandle::getFirstZeroBit(char* bitmap, int bitmapSize) {

}

// Method: bool isBitmapFull(char* bitmap, int bitmapSize)
// Check if the bitmap is all 1s
bool RM_FileHandle::isBitmapFull(char* bitmap, int bitmapSize) {

}

// Method: bool isBitmapEmpty(char* bitmap, int bitmapSize)
// Check if the bitmap is all 0s
bool RM_FileHandle::isBitmapEmpty(char* bitmap, int bitmapSize) {

}