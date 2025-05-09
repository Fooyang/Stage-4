#include "heapfile.h"
#include "error.h"

// routine to create a heapfile
const Status createHeapFile(const string fileName)
{
    File* 		file;
    Status 		status;
    FileHdrPage*	hdrPage;
    int			hdrPageNo;
    int			newPageNo;
    Page*		newPage;

    // try to open the file. This should return an error
    status = db.openFile(fileName, file);
    if (status != OK)
    {
	// create and allocate empty data and header page 
	// for nonexistent file
        status = db.createFile(fileName);
        if (status != OK) return status;
        
        // open file
        status = db.openFile(fileName, file);
        if (status != OK) return status;
        
        // allocate header page
        status = bufMgr->allocPage(file, hdrPageNo, newPage);
        if (status != OK) return status;
        
        hdrPage = (FileHdrPage*) newPage;
        
        strcpy(hdrPage->fileName, fileName.c_str());
        
        // allocate first data page
        status = bufMgr->allocPage(file, newPageNo, newPage);
        if (status != OK) return status;
        
        newPage->init(newPageNo);
        hdrPage->firstPage = newPageNo;
        hdrPage->lastPage = newPageNo;
        hdrPage->pageCnt = 1;
        hdrPage->recCnt = 0;
        
	// unpin pages
        status = bufMgr->unPinPage(file, hdrPageNo, true);
        if (status != OK) return status;
        
        status = bufMgr->unPinPage(file, newPageNo, true);
        if (status != OK) return status;

        status = db.closeFile(file);
        if (status != OK) return status;
        
        return OK;
    }
    return (FILEEXISTS); // file already exists
}

// routine to destroy a heapfile
const Status destroyHeapFile(const string fileName)
{
	return (db.destroyFile (fileName));
}

// constructor opens the underlying file
HeapFile::HeapFile(const string & fileName, Status& returnStatus)
{
    Status 	status;
    Page*	pagePtr;

    cout << "opening file " << fileName << endl;

    // open the file and read in the header page and the first data page
    if ((status = db.openFile(fileName, filePtr)) == OK)
    {
        status = filePtr->getFirstPage(headerPageNo);
        if (status != OK)
        {
            returnStatus = status;
            return;
        }
        
        status = bufMgr->readPage(filePtr, headerPageNo, pagePtr);
        if (status != OK)
        {
            returnStatus = status;
            return;
        }
        
        headerPage = (FileHdrPage*) pagePtr;
        hdrDirtyFlag = false;
        
        if (headerPage->firstPage != -1)
        {
            status = bufMgr->readPage(filePtr, headerPage->firstPage, curPage);
            if (status != OK)
            {
                bufMgr->unPinPage(filePtr, headerPageNo, false);
                returnStatus = status;
                return;
            }
            curPageNo = headerPage->firstPage;
            curDirtyFlag = false;
        }
        else
        {
            curPage = NULL;
            curPageNo = -1;
            curDirtyFlag = false;
        }
        
        curRec = NULLRID;
        returnStatus = OK;
    }
    else
    {
    	cerr << "open of heap file failed\n";
		returnStatus = status;
		return;
    }
}

// the destructor closes the file
HeapFile::~HeapFile()
{
    Status status;
    cout << "invoking heapfile destructor on file " << headerPage->fileName << endl;

    // see if there is a pinned data page. If so, unpin it 
    if (curPage != NULL)
    {
    	status = bufMgr->unPinPage(filePtr, curPageNo, curDirtyFlag);
		curPage = NULL;
		curPageNo = 0;
		curDirtyFlag = false;
		if (status != OK) cerr << "error in unpin of date page\n";
    }
	
	 // unpin header
    status = bufMgr->unPinPage(filePtr, headerPageNo, hdrDirtyFlag);
    if (status != OK) cerr << "error in unpin of header page\n";
	
	status = db.closeFile(filePtr);
    if (status != OK)
    {
		cerr << "error in closefile call\n";
		Error e;
		e.print (status);
    }
}

// Return number of records in heap file

const int HeapFile::getRecCnt() const
{
  return headerPage->recCnt;
}

// retrieve an arbitrary record from a file.
// if record is not on the currently pinned page, the current page
// is unpinned and the required page is read into the buffer pool
// and pinned.  returns a pointer to the record via the rec parameter

const Status HeapFile::getRecord(const RID & rid, Record & rec)
{
    Status status;

    // cout<< "getRecord. record (" << rid.pageNo << "." << rid.slotNo << ")" << endl;
   
    // check if page is valid
    if (rid.pageNo < 0) {
        return BADPAGENO;
    }

    // read in page if record is not on current page
    if (curPage == NULL || rid.pageNo != curPageNo)
    {
        // unpin current page if it exists
        if (curPage != NULL)
        {
            status = bufMgr->unPinPage(filePtr, curPageNo, curDirtyFlag);
            if (status != OK) return status;
            curPage = NULL;
        }
        
        // read in page
        status = bufMgr->readPage(filePtr, rid.pageNo, curPage);
        if (status != OK) {
            return status;
        }
        curPageNo = rid.pageNo;
        curDirtyFlag = false;
    }
    
    // obtain record
    status = curPage->getRecord(rid, rec);
    if (status != OK) {
        return status;
    }
    
    curRec = rid;
    return OK;
}

HeapFileScan::HeapFileScan(const string & name,
			   Status & status) : HeapFile(name, status)
{
    filter = NULL;
}

const Status HeapFileScan::startScan(const int offset_,
				     const int length_,
				     const Datatype type_, 
				     const char* filter_,
				     const Operator op_)
{
    if (!filter_) {                        // no filtering requested
        filter = NULL;
        return OK;
    }
    
    if ((offset_ < 0 || length_ < 1) ||
        (type_ != STRING && type_ != INTEGER && type_ != FLOAT) ||
        (type_ == INTEGER && length_ != sizeof(int)
         || type_ == FLOAT && length_ != sizeof(float)) ||
        (op_ != LT && op_ != LTE && op_ != EQ && op_ != GTE && op_ != GT && op_ != NE))
    {
        return BADSCANPARM;
    }

    offset = offset_;
    length = length_;
    type = type_;
    filter = filter_;
    op = op_;

    return OK;
}


const Status HeapFileScan::endScan()
{
    Status status;
    // generally must unpin last page of the scan
    if (curPage != NULL)
    {
        status = bufMgr->unPinPage(filePtr, curPageNo, curDirtyFlag);
        curPage = NULL;
        curPageNo = 0;
		curDirtyFlag = false;
        return status;
    }
    return OK;
}

HeapFileScan::~HeapFileScan()
{
    endScan();
}

const Status HeapFileScan::markScan()
{
    // make a snapshot of the state of the scan
    markedPageNo = curPageNo;
    markedRec = curRec;
    return OK;
}

const Status HeapFileScan::resetScan()
{
    Status status;
    if (markedPageNo != curPageNo) 
    {
		if (curPage != NULL)
		{
			status = bufMgr->unPinPage(filePtr, curPageNo, curDirtyFlag);
			if (status != OK) return status;
		}
		// restore curPageNo and curRec values
		curPageNo = markedPageNo;
		curRec = markedRec;
		// then read the page
		status = bufMgr->readPage(filePtr, curPageNo, curPage);
		if (status != OK) return status;
		curDirtyFlag = false; // it will be clean
    }
    else curRec = markedRec;
    return OK;
}


const Status HeapFileScan::scanNext(RID& outRid)
{
    Status 	status = OK;
    RID		nextRid;
    RID		tmpRid;
    int 	nextPageNo;
    Record  rec;

    // start from beginning if no curr page
    if (curPage == NULL) {
        if (headerPage->firstPage == -1) {
            return NORECORDS;
        }
        status = bufMgr->readPage(filePtr, headerPage->firstPage, curPage);
        if (status != OK) return status;
        curPageNo = headerPage->firstPage;
        curDirtyFlag = false;
        
        // get first record
        status = curPage->firstRecord(tmpRid);
        if (status != OK) {
            if (status == NORECORDS) {
                return scanNext(outRid);
            }
            return status;
        }
        curRec = tmpRid;
    }

    // try to obtain next record
    status = curPage->nextRecord(curRec, nextRid);
    if (status == OK) {
        curRec = nextRid;
    } else if (status == ENDOFPAGE) {
        // need to move to next page
        status = curPage->getNextPage(nextPageNo);
        if (status != OK) return status;
        
        if (nextPageNo == -1) {
            return FILEEOF;
        }
        
        // unpin current page
        status = bufMgr->unPinPage(filePtr, curPageNo, curDirtyFlag);
        if (status != OK) return status;
        
        // read next page
        status = bufMgr->readPage(filePtr, nextPageNo, curPage);
        if (status != OK) return status;
        curPageNo = nextPageNo;
        curDirtyFlag = false;
        
        // get first record on new page
        status = curPage->firstRecord(tmpRid);
        if (status != OK) {
            if (status == NORECORDS) {
                return scanNext(outRid);
            }
            return status;
        }
        curRec = tmpRid;
    } else {
        return status;
    }

    // return if no filter
    if (filter == NULL) {
        outRid = curRec;
        return OK;
    }

    // if filter, check if record matches filter
    status = curPage->getRecord(curRec, rec);
    if (status != OK) return status;

    if (matchRec(rec)) {
        outRid = curRec;
        return OK;
    }

    // try next if not matching
    return scanNext(outRid);
}


// returns pointer to the current record.  page is left pinned
// and the scan logic is required to unpin the page 

const Status HeapFileScan::getRecord(Record & rec)
{
    return curPage->getRecord(curRec, rec);
}

// delete record from file. 
const Status HeapFileScan::deleteRecord()
{
    Status status;

    // delete the "current" record from the page
    status = curPage->deleteRecord(curRec);
    curDirtyFlag = true;

    // reduce count of number of records in the file
    headerPage->recCnt--;
    hdrDirtyFlag = true; 
    return status;
}


// mark current page of scan dirty
const Status HeapFileScan::markDirty()
{
    curDirtyFlag = true;
    return OK;
}

const bool HeapFileScan::matchRec(const Record & rec) const
{
    // no filtering requested
    if (!filter) return true;

    // see if offset + length is beyond end of record
    // maybe this should be an error???
    if ((offset + length -1 ) >= rec.length)
	return false;

    float diff = 0;                       // < 0 if attr < fltr
    switch(type) {

    case INTEGER:
        int iattr, ifltr;                 // word-alignment problem possible
        memcpy(&iattr,
               (char *)rec.data + offset,
               length);
        memcpy(&ifltr,
               filter,
               length);
        diff = iattr - ifltr;
        break;

    case FLOAT:
        float fattr, ffltr;               // word-alignment problem possible
        memcpy(&fattr,
               (char *)rec.data + offset,
               length);
        memcpy(&ffltr,
               filter,
               length);
        diff = fattr - ffltr;
        break;

    case STRING:
        diff = strncmp((char *)rec.data + offset,
                       filter,
                       length);
        break;
    }

    switch(op) {
    case LT:  if (diff < 0.0) return true; break;
    case LTE: if (diff <= 0.0) return true; break;
    case EQ:  if (diff == 0.0) return true; break;
    case GTE: if (diff >= 0.0) return true; break;
    case GT:  if (diff > 0.0) return true; break;
    case NE:  if (diff != 0.0) return true; break;
    }

    return false;
}

InsertFileScan::InsertFileScan(const string & name,
                               Status & status) : HeapFile(name, status)
{
  //Do nothing. Heapfile constructor will bread the header page and the first
  // data page of the file into the buffer pool
}

InsertFileScan::~InsertFileScan()
{
    Status status;
    // unpin last page of the scan
    if (curPage != NULL)
    {
        status = bufMgr->unPinPage(filePtr, curPageNo, true);
        curPage = NULL;
        curPageNo = 0;
        if (status != OK) cerr << "error in unpin of data page\n";
    }
}

// Insert a record into the file
const Status InsertFileScan::insertRecord(const Record & rec, RID& outRid)
{
    Page*	newPage;
    int		newPageNo;
    Status	status, unpinstatus;
    RID		rid;

    // If no current page, start with the last page
    if (curPage == NULL) {
        if (headerPage->lastPage == -1) {
            // File is empty, allocate first page
            status = bufMgr->allocPage(filePtr, newPageNo, newPage);
            if (status != OK) return status;
            
            newPage->init(newPageNo);
            headerPage->firstPage = newPageNo;
            headerPage->lastPage = newPageNo;
            headerPage->pageCnt = 1;
            
            curPage = newPage;
            curPageNo = newPageNo;
            curDirtyFlag = true;
            hdrDirtyFlag = true;
        } else {
            // Read the last page
            status = bufMgr->readPage(filePtr, headerPage->lastPage, curPage);
            if (status != OK) return status;
            curPageNo = headerPage->lastPage;
            curDirtyFlag = false;
        }
    }

    // Try to insert the record on the current page
    status = curPage->insertRecord(rec, rid);
    if (status == OK) {
        // Record inserted successfully
        outRid = rid;
        headerPage->recCnt++;
        hdrDirtyFlag = true;
        curDirtyFlag = true;
        return OK;
    }

    if (status != NOSPACE) {
        // Some error other than no space
        return status;
    }

    // Current page is full, allocate a new page
    status = bufMgr->allocPage(filePtr, newPageNo, newPage);
    if (status != OK) return status;

    // Initialize the new page
    newPage->init(newPageNo);
    
    // Link the new page to the last page
    status = curPage->setNextPage(newPageNo);
    if (status != OK) {
        bufMgr->unPinPage(filePtr, newPageNo, false);
        return status;
    }

    // Update header page
    headerPage->lastPage = newPageNo;
    headerPage->pageCnt++;
    hdrDirtyFlag = true;

    // Unpin the old page
    unpinstatus = bufMgr->unPinPage(filePtr, curPageNo, curDirtyFlag);
    if (unpinstatus != OK) return unpinstatus;

    // Make the new page the current page
    curPage = newPage;
    curPageNo = newPageNo;
    curDirtyFlag = true;

    // Try to insert the record on the new page
    status = curPage->insertRecord(rec, rid);
    if (status != OK) return status;

    // Record inserted successfully
    outRid = rid;
    headerPage->recCnt++;
    hdrDirtyFlag = true;
    return OK;
}
