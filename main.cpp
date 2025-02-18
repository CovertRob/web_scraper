#include <include/cef_app.h>
#include <include/cef_browser.h>
#include <include/cef_client.h>
#include <include/cef_render_handler.h>
#include <include/cef_life_span_handler.h>
#include <include/cef_display_handler.h>
#include <include/cef_string_visitor.h>

#include <iostream>
#include <fstream>
#include <string>
#include <vector>
#include <regex>

// Cross-platform sleep for eventul push to prod
#ifdef _WIN32
# include <Windows.h>
#else
# include <unistd.h>
#endif

static void SleepMs(int ms) {
#ifdef _WIN32
    Sleep(ms);
#else
    usleep(ms * 1000);
#endif
}

// Forward declaration
class GoogleClient;

// Visitor for scraping each job page’s raw HTML
class JobPageScraperVisitor : public CefStringVisitor {
public:
    explicit JobPageScraperVisitor(GoogleClient* client)
        : client_(client) {}

    void Visit(const CefString& content) override;

private:
    GoogleClient* client_;
    IMPLEMENT_REFCOUNTING(JobPageScraperVisitor);
};

//------------ Main CEF Client ------------------------------------
class GoogleClient : public CefClient,
                     public CefLoadHandler,
                     public CefRenderHandler,
                     public CefLifeSpanHandler,
                     public CefDisplayHandler  // <-- Important for OnConsoleMessage
{
public:
    GoogleClient()
        : current_link_index_(0),
          page_count_(0),
          max_pages_(3),
          state_(State::SEARCHING)
    {
    }

    // Enumerations for states
    enum class State {
        SEARCHING,
        SCRAPING_JOBS
    };

    // CEF Handler Accessors
    CefRefPtr<CefLoadHandler> GetLoadHandler() override {
        return this;
    }
    CefRefPtr<CefRenderHandler> GetRenderHandler() override {
        return this;
    }
    CefRefPtr<CefLifeSpanHandler> GetLifeSpanHandler() override {
        return this;
    }
    // Must implement this for OnConsoleMessage to override properly
    CefRefPtr<CefDisplayHandler> GetDisplayHandler() override {
        return this;
    }

    // OnAfterCreated: store browser
    void OnAfterCreated(CefRefPtr<CefBrowser> browser) override {
        // This might be called on the UI thread, so be sure to check macros if needed:
        // CEF_REQUIRE_UI_THREAD();
        main_browser_ = browser;
        std::cout << "[GoogleClient] OnAfterCreated: stored main_browser_\n";
    }

    // OnLoadEnd: main logic
    void OnLoadEnd(CefRefPtr<CefBrowser> browser,
                   CefRefPtr<CefFrame> frame,
                   int httpStatusCode) override
    {
        // Must include this so browser doesn't execute OnLoadEnd scripting on every subframe it loads. Will lock up and the html scrape of main page will fail otherwise
        if (!frame->IsMain()) {
            return; // exit call and let subframe/worker load in background
        }
        std::string url = frame->GetURL();
        std::cout << "[OnLoadEnd] Page loaded: " << url
                  << " (HTTP " << httpStatusCode << ")\n";

        // Searching mode: inject JS that grabs #search results
        if (state_ == State::SEARCHING) {
            // This JavaScript will find up to ~10 organic results in the "#search" container and skip any link containing "googleadservices" or "google.com"
            static const std::string getLinksJS = R"(
                (function() {
                  let container = document.getElementById('search');
                  if (!container) {
                    console.log('SCRAPE_RESULTS:[]');
                    return;
                  }
                  // Typically ~10 results in .g or .yuRUbf
                  let anchors = container.querySelectorAll('div.g a');
                  let results = [];
                  anchors.forEach(a => {
                    if (!a.href.includes('googleadservices') &&
                        !a.href.includes('google.com')) {
                        results.push(a.href);
                    }
                  });
                  console.log('SCRAPE_RESULTS:' + JSON.stringify(results));
                })();
            )";

            // Inject the JS
            frame->ExecuteJavaScript(getLinksJS, frame->GetURL(), 0);
        }
        else if (state_ == State::SCRAPING_JOBS) {
            // We loaded an actual job page -> grab the HTML
            frame->GetSource(new JobPageScraperVisitor(this));
        }
    }

    
    // Parse "SCRAPE_RESULTS:[...]" logs from JS console
    bool OnConsoleMessage(CefRefPtr<CefBrowser> browser,
                          cef_log_severity_t level,
                          const CefString& message,
                          const CefString& source,
                          int line) override
    {
        std::string msg = message.ToString();
        // Our magic prefix
        const std::string kPrefix = "SCRAPE_RESULTS:";
        if (msg.rfind(kPrefix, 0) == 0) {
            // Extract the JSON substring
            std::string jsonPart = msg.substr(kPrefix.size());
            std::cout << "[OnConsoleMessage] Got search results JSON: " << jsonPart << "\n";

            // Quick/naive parse of JSON array (like ["link1","link2",...])
            std::regex linkRegex("\"([^\"]+)\""); // captures text inside quotes
            auto begin = std::sregex_iterator(jsonPart.begin(), jsonPart.end(), linkRegex);
            auto end   = std::sregex_iterator();

            for (auto it = begin; it != end; ++it) {
                std::string link = (*it)[1].str();
                job_links_.push_back(link);
            }

            // Once we've stored them for this page, either proceed to next or done
            SleepMs(1500);
            if (page_count_ < max_pages_) {
                page_count_++;
                NavigateToNextPage();
            } else {
                BeginScrapingLinks();
            }
        }
        return false;  // allow normal console logging
    }

    // Off-screen painting stubs
    void OnPaint(CefRefPtr<CefBrowser>,
                 PaintElementType,
                 const RectList&,
                 const void*,
                 int,
                 int) override
    {
        // no-op
    }

    // The size of our invisible viewport
    void GetViewRect(CefRefPtr<CefBrowser>, CefRect& rect) override {
        rect = CefRect(0, 0, 1280, 800);
    }


    // Moves to next Google search page
    void NavigateToNextPage() {
        if (!main_browser_) {
            std::cerr << "[GoogleClient] Error: main_browser_ is null.\n";
            CefQuitMessageLoop();
            return;
        }
        CefRefPtr<CefFrame> frame = main_browser_->GetMainFrame();
        if (!frame) {
            std::cerr << "[GoogleClient] Error: main frame is null.\n";
            CefQuitMessageLoop();
            return;
        }

        std::cout << "[GoogleClient] Attempting to navigate to next page...\n";

        // Simple JS: click on 'pnnext' link if present
        std::string nextPageJS = R"(
            (function() {
                var nextLink = document.getElementById('pnnext');
                if (nextLink) {
                    window.location.href = nextLink.href;
                } else {
                    console.log('No next link found');
                    // If no next link found, produce empty array so we proceed
                    console.log('SCRAPE_RESULTS:[]');
                }
            })();
        )";

        frame->ExecuteJavaScript(nextPageJS, frame->GetURL(), 0);
    }

    // Switch from searching to scraping actual links
    void BeginScrapingLinks() {
        if (job_links_.empty()) {
            std::cout << "[GoogleClient] No job links found. Quitting.\n";
            CefQuitMessageLoop();
            return;
        }
        std::cout << "[GoogleClient] Found " << job_links_.size()
                  << " total links. Switching to SCRAPING_JOBS.\n";

        state_ = State::SCRAPING_JOBS;
        current_link_index_ = 0;

        NavigateToJobLink(job_links_[current_link_index_]);
    }

    // Navigate to a single job link
    void NavigateToJobLink(const std::string& url) {
        if (!main_browser_) {
            std::cerr << "[GoogleClient] main_browser_ is null.\n";
            CefQuitMessageLoop();
            return;
        }
        CefRefPtr<CefFrame> frame = main_browser_->GetMainFrame();
        if (!frame) {
            std::cerr << "[GoogleClient] main frame is null.\n";
            CefQuitMessageLoop();
            return;
        }

        std::cout << "[GoogleClient] Navigating to job link #"
                  << current_link_index_ << ": " << url << "\n";

        // Optionally skip google links
        if (url.find("google") == std::string::npos) {
            frame->LoadURL(url);
        } else {
            std::cout << "Skipping google link: " << url << "\n";
            OnJobPageScraped(); // proceed to next
        }
    }

    // Called after job page is saved
    void OnJobPageScraped() {
        current_link_index_++;
        if (current_link_index_ < job_links_.size()) {
            NavigateToJobLink(job_links_[current_link_index_]);
        } else {
            std::cout << "[GoogleClient] Finished scraping all jobs.\n";
            CefQuitMessageLoop();
        }
    }

private:
    friend class JobPageScraperVisitor;

    State state_;
    size_t current_link_index_;
    size_t page_count_;
    size_t max_pages_;

    // Where we store final extracted links
    std::vector<std::string> job_links_;

    // A reference to the main browser
    CefRefPtr<CefBrowser> main_browser_;

    IMPLEMENT_REFCOUNTING(GoogleClient);
};

// JobPageScraperVisitor: writes each job page's HTML to file
void JobPageScraperVisitor::Visit(const CefString& content) {
    static size_t s_fileIndex = 0;
    s_fileIndex++;

    std::string html = content.ToString();
    std::string filename = "job_page_" + std::to_string(s_fileIndex) + ".html";

    std::ofstream ofs(filename);
    if (ofs) {
        ofs << html;
        ofs.close();
        std::cout << "[JobPageScraperVisitor] Saved HTML to " << filename << "\n";
    } else {
        std::cerr << "[JobPageScraperVisitor] Could not open file: " << filename << "\n";
    }

    // Signal to client we're done with this page
    client_->OnJobPageScraped();
}

// ------------------ main -------------------------------------
// Use flag --Search_Parameters to pass search parameters directly
int main(int argc, char* argv[]) {

    CefMainArgs main_args(argc, argv);
    // Don't parse flags if a sub-process
    int exit_code = CefExecuteProcess(main_args, nullptr, nullptr);
    if (exit_code >= 0) {
        // This is a sub-process – just return immediately.
        return exit_code;
    }

    // Note that CEF will restart main and pass sub-process flags to itself as needed so argc count will not remain constant. Must parse custom flags prior to initializing CEF otherwise they will be stripped.
    using namespace std::literals;
    std::string base_search_url = "https://www.google.com/search?q=";
    std::string search_parameters;
    std::string combined_url;
    // Fetches an iterator starting at the --Search_Parameters flag position
    // Remember that last position in array is a null pointer
    if (argc > 1) {
        auto flag = std::find_if(main_args.argv, main_args.argv + main_args.argc, 
            [](const char* arg) {
            return std::string_view(arg) == "--Search_Parameters"sv;
        });
        // Perform boundary checks
        // Avoid comparing null ptrs
        if (flag < main_args.argv + main_args.argc - 1) {
            search_parameters = std::string(*(flag + 1));
        }
        combined_url = base_search_url + search_parameters;
    }
    
    // To-Do: Add search_parameters.JSON file check here

    if (combined_url == base_search_url) {
        // Exit program if no parameters specified
        std::cout << "Must specify search parameters. Exiting." << std::endl;
        exit(0);
    }
    // If passing via CLI, take args as literal values
    // Assumed to be URL encoded if need be

    // CEF init
    CefSettings settings;
    settings.no_sandbox = true;
    settings.windowless_rendering_enabled = true;
    settings.command_line_args_disabled = false;
    // Initialize CEF
    CefInitialize(main_args, settings, nullptr, nullptr);

    // Create a windowless browser
    CefWindowInfo window_info;
    window_info.SetAsWindowless(0);

    CefBrowserSettings browser_settings;

    // Our custom client
    CefRefPtr<GoogleClient> client(new GoogleClient());

    // Create the browser
    CefBrowserHost::CreateBrowserSync(
        window_info,
        client.get(),
        combined_url,
        browser_settings,
        nullptr,
        nullptr
    );

    // Message loop until done
    CefRunMessageLoop();

    // Cleanup
    CefShutdown();
    return 0;
}