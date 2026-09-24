char *Data = malloc(Count + 1);
if (!Data) {
  return -12;
}

uc_err Error = uc_mem_read(Unicorn, Buffer, Data, Count);
if (Error != UC_ERR_OK) {
  free(Data);
  return -14;
}
Data[Count] = '\0';

uc_err Error = uc_mem_write(Unicorn, Buffer, Data, Count);
if (Error != UC_ERR_OK) {
  return -14;
}

uint64_t Result = sendfile(OutFd, InFd, OffsetAddress ? &Offset : NULL, Count);
if (Result < 0) {
  return -errno;
}
