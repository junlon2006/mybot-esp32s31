/* SPDX-License-Identifier: MIT */
#ifndef SSID_MANAGER_H
#define SSID_MANAGER_H

#include <mutex>
#include <string>
#include <vector>

struct SsidItem {
    std::string ssid;
    std::string password;
};

class SsidManager {
  public:
    static SsidManager &GetInstance() {
        static SsidManager instance;
        return instance;
    }

    bool AddSsid(const std::string &ssid, const std::string &password);
    bool RemoveSsid(int index);
    bool SetDefaultSsid(int index);
    bool Clear();
    std::vector<SsidItem> GetSsidList();

  private:
    SsidManager();
    ~SsidManager();

    bool LoadFromNvs();
    bool SaveToNvs(std::vector<SsidItem> &profiles);

    std::mutex mutex_;
    bool loaded_ = false;

    std::vector<SsidItem> ssid_list_;
};

#endif // SSID_MANAGER_H
