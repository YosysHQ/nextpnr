// This is intended to be included inside arch.h only.

template <typename T> struct RelPtr
{
    int32_t offset;

    const T *get() const { return reinterpret_cast<const T *>(reinterpret_cast<const char *>(this) + offset); }

    const T &operator[](size_t index) const { return get()[index]; }

    const T &operator*() const { return *(get()); }

    const T *operator->() const { return get(); }

    RelPtr(const RelPtr &) = delete;
    RelPtr &operator=(const RelPtr &) = delete;
};

NPNR_PACKED_STRUCT(template <typename T> struct RelSlice {
    int32_t offset;
    uint32_t length;

    const T *get() const { return reinterpret_cast<const T *>(reinterpret_cast<const char *>(this) + offset); }

    const T &operator[](size_t index) const
    {
        NPNR_ASSERT(index < length);
        return get()[index];
    }

    const T *begin() const { return get(); }
    const T *end() const { return get() + length; }

    const size_t size() const { return length; }

    const T &operator*() const { return *(get()); }

    const T *operator->() const { return get(); }

    RelSlice(const RelSlice &) = delete;
    RelSlice &operator=(const RelSlice &) = delete;
});
