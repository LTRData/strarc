#include <malloc.h>

class LinkTrackerItem
{

private:

  DWORD dwVolumeSerialNumber;
  LONGLONG NodeNumber;
  UNICODE_STRING Name;
  LinkTrackerItem *Next;

  LinkTrackerItem(LinkTrackerItem * _Next,
		  PUNICODE_STRING _Name,
		  DWORD _dwVolumeSerialNumber,
		  LONGLONG _NodeNumber)
    : Next(_Next),
      dwVolumeSerialNumber(_dwVolumeSerialNumber),
      NodeNumber(_NodeNumber & 0x0000FFFFFFFFFFFF)
  {
    Name.Buffer = (PWSTR) malloc(_Name->Length);

    if (Name.Buffer == NULL)
      return;

    Name.Length = _Name->Length;
    Name.MaximumLength = _Name->Length;

    RtlCopyUnicodeString(&Name, _Name);
  }

  ~LinkTrackerItem()
  {
    if (Name.Buffer != NULL)
      free(Name.Buffer);
  }

public:

  LinkTrackerItem *DeleteAndGetNext()
  {
    LinkTrackerItem *next_item = Next;
    delete this;
    return next_item;
  }

  static
  LinkTrackerItem *NewItem(LinkTrackerItem * Next,
			   PUNICODE_STRING Name,
			   DWORD dwVolumeSerialNumber,
			   LONGLONG NodeNumber)
  {
    LinkTrackerItem *item =
      new LinkTrackerItem(Next, Name, dwVolumeSerialNumber, NodeNumber);

    if (item == NULL)
      return NULL;

    if (item->Name.Buffer == NULL)
      return NULL;

    return item;
  }

  bool
  Match(DWORD _dwVolumeSerialNumber,
	LONGLONG _NodeNumber,
	PUNICODE_STRING * _Name)
  {
    if (this == NULL)
      return false;

    if ((dwVolumeSerialNumber == _dwVolumeSerialNumber) &&
	(NodeNumber == (_NodeNumber & 0x0000FFFFFFFFFFFF)))
      {
	*_Name = &Name;
	return true;
      }

    return false;
  }

  LinkTrackerItem *GetNext()
  {
    if (this == NULL)
      return NULL;

    return Next;
  }
};
