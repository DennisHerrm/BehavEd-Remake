#include "bhed/arbeiter.h"

#include <algorithm>
#include <atomic>
#include <condition_variable>
#include <mutex>
#include <thread>
#include <vector>

namespace bhed::arbeiter {
namespace {

// Der eine Auftrag, an dem gerade gearbeitet wird.
struct Auftrag {
    const std::function<void(std::size_t)>* je = nullptr;
    std::size_t n = 0;
    std::atomic<std::size_t> naechster{0};
    std::atomic<std::size_t> erledigt{0};
};

class Mannschaft {
public:
    Mannschaft() {
        const unsigned kerne = std::max(1U, std::thread::hardware_concurrency());
        // Einer bleibt fuer den Aufrufer; hoechstens 31 Helfer.
        const unsigned helfer = std::min(kerne, 32U) - 1U;
        faeden_.reserve(helfer);
        for (unsigned i = 0; i < helfer; ++i) {
            faeden_.emplace_back([this] { arbeite(); });
        }
    }
    ~Mannschaft() {
        {
            const std::lock_guard<std::mutex> sperre(sperre_);
            ende_ = true;
            ++runde_;
        }
        weck_.notify_all();
        for (std::thread& t : faeden_) {
            if (t.joinable()) {
                t.join();
            }
        }
    }
    Mannschaft(const Mannschaft&) = delete;
    Mannschaft& operator=(const Mannschaft&) = delete;

    [[nodiscard]] unsigned groesse() const { return static_cast<unsigned>(faeden_.size()) + 1U; }

    // false, wenn schon ein Auftrag laeuft - dann erledigt der Aufrufer selbst.
    bool fuehreAus(std::size_t n, const std::function<void(std::size_t)>& je) {
        bool frei = false;
        if (!belegt_.compare_exchange_strong(frei, true)) {
            return false;
        }
        {
            const std::lock_guard<std::mutex> sperre(sperre_);
            auftrag_.je = &je;
            auftrag_.n = n;
            auftrag_.naechster.store(0);
            auftrag_.erledigt.store(0);
            ++runde_;
        }
        weck_.notify_all();
        bearbeite();
        // Warten, bis auch die Helfer ihre letzten Plaetze abgeschlossen haben.
        {
            std::unique_lock<std::mutex> sperre(sperre_);
            fertig_.wait(sperre, [this] { return auftrag_.erledigt.load() >= auftrag_.n && aktive_ == 0; });
            // Unter der Sperre: ein Helfer, der erst jetzt aufwacht, sieht
            // dann sicher "kein Auftrag" statt eines halb geloeschten.
            auftrag_.je = nullptr;
        }
        belegt_.store(false);
        return true;
    }

private:
    void bearbeite() {
        const std::function<void(std::size_t)>& je = *auftrag_.je;
        for (std::size_t i = auftrag_.naechster.fetch_add(1); i < auftrag_.n; i = auftrag_.naechster.fetch_add(1)) {
            je(i);
            auftrag_.erledigt.fetch_add(1);
        }
    }
    void arbeite() {
        std::size_t gesehen = 0;
        for (;;) {
            {
                std::unique_lock<std::mutex> sperre(sperre_);
                weck_.wait(sperre, [&] { return runde_ != gesehen; });
                gesehen = runde_;
                if (ende_) {
                    return;
                }
                if (auftrag_.je == nullptr) {
                    continue;
                }
                ++aktive_;
            }
            bearbeite();
            {
                const std::lock_guard<std::mutex> sperre(sperre_);
                --aktive_;
            }
            fertig_.notify_all();
        }
    }

    std::vector<std::thread> faeden_;
    std::mutex sperre_;
    std::condition_variable weck_;
    std::condition_variable fertig_;
    std::size_t runde_ = 0;
    int aktive_ = 0;
    bool ende_ = false;
    std::atomic<bool> belegt_{false};
    Auftrag auftrag_;
};

Mannschaft& mannschaft() {
    static Mannschaft m;
    return m;
}

// Laeuft dieser Faden gerade in einer Aufgabe? Dann innen ohne Faeden.
thread_local bool g_inAufgabe = false;

}  // namespace

void verteilt(std::size_t n, const std::function<void(std::size_t)>& je, std::size_t abWann) {
    if (n == 0U) {
        return;
    }
    if (n < abWann || g_inAufgabe || mannschaft().groesse() <= 1U) {
        for (std::size_t i = 0; i < n; ++i) {
            je(i);
        }
        return;
    }
    // Die Helfer setzen g_inAufgabe in ihrem eigenen Faden ueber diese Huelle.
    const std::function<void(std::size_t)> huelle = [&je](std::size_t i) {
        const bool vorher = g_inAufgabe;
        g_inAufgabe = true;
        je(i);
        g_inAufgabe = vorher;
    };
    if (!mannschaft().fuehreAus(n, huelle)) {
        for (std::size_t i = 0; i < n; ++i) {
            je(i);
        }
    }
}

unsigned anzahl() { return mannschaft().groesse(); }

}  // namespace bhed::arbeiter
