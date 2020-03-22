class LinkInfo
{
  DWORD dwVolumeSerialNumber;
  LONGLONG NodeNumber;
  LPWSTR wczName;
  LinkInfo *Next;

public:
    LinkInfo(LinkInfo * _Next, LPCWSTR _wczName, DWORD _dwVolumeSerialNumber,
	     LONGLONG _NodeNumber):Next(_Next), wczName(wcsdup(_wczName)),
    dwVolumeSerialNumber(_dwVolumeSerialNumber),
    NodeNumber(_NodeNumber & 0x0000FFFFFFFFFFFF)
  {
    if (wczName == NULL)
      status_exit(XE_NOT_ENOUGH_MEMORY_FOR_LINK_TRACKER);
  }

  bool Match(DWORD _dwVolumeSerialNumber, LONGLONG _NodeNumber,
	     LPCWSTR * _wczName) const
  {
    if (this == NULL)
      return false;

    if ((dwVolumeSerialNumber == _dwVolumeSerialNumber) &&
	(NodeNumber == (_NodeNumber & 0x0000FFFFFFFFFFFF)))
      *_wczName = wczName;
    else
      return false;

    return true;
  }

  LinkInfo *GetNext()
  {
    if (this == NULL)
      return NULL;

    return Next;
  }
};

extern LinkInfo *LinkTracker[256];

inline LPCWSTR
MatchLink(DWORD dwVolumeSerialNumber, LONGLONG NodeNumber, LPCWSTR wczName)
{
  LPCWSTR wczLinkName = NULL;

  for (LinkInfo * linkinfo = LinkTracker[(NodeNumber & 0xFF0) >> 4];
       linkinfo != NULL; linkinfo = linkinfo->GetNext())
    {
      YieldSingleProcessor();

      if (linkinfo->Match(dwVolumeSerialNumber, NodeNumber, &wczLinkName))
	return wczLinkName;
    }

  LinkInfo *NewLinkInfo =
    new LinkInfo(LinkTracker[(NodeNumber & 0xFF0) >> 4], wczName,
		 dwVolumeSerialNumber, NodeNumber);

  if (NewLinkInfo == NULL)
    status_exit(XE_NOT_ENOUGH_MEMORY_FOR_LINK_TRACKER);

  LinkTracker[(NodeNumber & 0xFF0) >> 4] = NewLinkInfo;

  return NULL;
}
