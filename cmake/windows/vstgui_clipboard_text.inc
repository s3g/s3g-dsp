    if (format->cfFormat == CF_TEXT || format->cfFormat == CF_UNICODETEXT)
    {
        for (uint32_t i = 0; i < dataPackage->getCount (); ++i)
        {
            if (dataPackage->getDataType (i) != IDataPackage::kText)
                continue;
            const void* buffer = nullptr;
            IDataPackage::Type type;
            const uint32_t bufferSize = dataPackage->getData (i, buffer, type);
            UTF8StringHelper utf8String (static_cast<const char*> (buffer), bufferSize);
            const void* data = buffer;
            SIZE_T bytes = bufferSize;
            SIZE_T terminator = sizeof (char);
            if (format->cfFormat == CF_UNICODETEXT)
            {
                const auto* wide = utf8String.getWideString ();
                data = wide;
                // UTF-8 bytes are not a count of UTF-16 code units.
                bytes = wide ? wcslen (wide) * sizeof (WCHAR) : 0;
                terminator = sizeof (WCHAR);
            }
            if (data && bytes > 0)
            {
                // Windows requires a terminating character. Allocate it
                // separately so the source buffer is never read past its end.
                HGLOBAL memoryHandle = GlobalAlloc (GMEM_MOVEABLE | GMEM_ZEROINIT,
                                                    bytes + terminator);
                if (!memoryHandle)
                    return E_OUTOFMEMORY;
                void* memory = GlobalLock (memoryHandle);
                if (!memory)
                {
                    GlobalFree (memoryHandle);
                    return E_OUTOFMEMORY;
                }
                memcpy (memory, data, bytes);
                GlobalUnlock (memoryHandle);
                medium->hGlobal = memoryHandle;
                medium->tymed = TYMED_HGLOBAL;
                return S_OK;
            }
        }
    }
