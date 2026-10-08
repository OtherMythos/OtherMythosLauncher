#include "Ui.h"

#include "Theme.h"
#include "TimeFormat.h"
#include "Version.h"
#include "Widgets.h"

#include <algorithm>
#include <cfloat>
#include <ctime>
#include <imgui.h>
#include <imgui_internal.h>

namespace OML{
    namespace{
        bool pressedMenu(){ return pressed(ImGuiKey_GamepadStart, ImGuiKey_F1); }
        bool pressedDownload(){ return pressed(ImGuiKey_GamepadFaceLeft, ImGuiKey_D); }
        bool pressedDelete(){ return pressed(ImGuiKey_GamepadFaceUp, ImGuiKey_Delete); }
        bool pressedPrevious(){ return pressed(ImGuiKey_GamepadL1, ImGuiKey_PageUp, ImGuiKey_None, true); }
        bool pressedNext(){ return pressed(ImGuiKey_GamepadR1, ImGuiKey_PageDown, ImGuiKey_None, true); }
        bool pressedRefresh(){ return pressed(ImGuiKey_GamepadBack, ImGuiKey_F5); }

        const DownloadView* findDownload(const AppView& view, const std::string& project, const std::string& buildId, const std::string& job){
            for(const DownloadView& d : view.downloads){
                if(d.project == project && d.buildId == buildId && d.job == job) return &d;
            }
            return nullptr;
        }

        std::string buildTime(const CatalogBuild& build){
            int64_t t = 0;
            return parseIsoUtc(build.committed, t) ? formatLocalShort(t) : build.id;
        }

        std::string shortCommit(const CatalogBuild& build){
            return build.commit.empty() ? build.id : build.commit;
        }

        std::string downloadLabel(const DownloadView& d){
            if(!d.running) return "Waiting to download";
            int percent = d.total ? int(d.done * 100 / d.total) : 0;
            std::string label = std::to_string(percent) + "%  ·  " + formatSize(d.done) + " of " + formatSize(d.total);
            if(d.bytesPerSecond > 0) label += "  ·  " + formatSize(uint64_t(d.bytesPerSecond)) + "/s";
            return label;
        }

        //What the detail pane leads with: the newest build of the preferred job (Release where
        //there is one), and the newest copy of that job that's already installed.
        struct Featured{
            const CatalogBuild* latest = nullptr;
            const CatalogJob* latestJob = nullptr;
            const CatalogBuild* installed = nullptr;
            const CatalogJob* installedJob = nullptr;
        };

        Featured featured(const CatalogProject& project){
            Featured f;
            std::string preferred;
            for(const CatalogBuild& b : project.builds){
                for(const CatalogJob& j : b.jobs){
                    if(jobBuildType(j.name) == "Release"){
                        preferred = j.name;
                        break;
                    }
                }
                if(!preferred.empty()) break;
            }
            if(preferred.empty() && !project.builds.empty() && !project.builds[0].jobs.empty()) preferred = project.builds[0].jobs[0].name;
            for(const CatalogBuild& b : project.builds){
                for(const CatalogJob& j : b.jobs){
                    if(j.name != preferred) continue;
                    if(!f.latest){
                        f.latest = &b;
                        f.latestJob = &j;
                    }
                    if(j.installed && !f.installed){
                        f.installed = &b;
                        f.installedJob = &j;
                    }
                }
            }
            return f;
        }
    }

    Ui::Ui(App& app)
        : mApp(app),
          mLastFocusKind(FocusKind::NONE),
          mFocusProjectRequest(true),
          mFocusPrimaryRequest(false),
          mOpenMenuRequest(false),
          mOpenDeleteRequest(false),
          mOpenQuitRequest(false),
          mFocusQuitRequest(false),
          mLogo(0),
          mPopupOpenedFrame(-1) {
    }

    std::string Ui::title(const AppView& view, const std::string& project) const{
        auto it = view.titles.find(project);
        return it == view.titles.end() ? project : it->second;
    }

    void Ui::drawHeader(const AppView& view, Focus& focus, UiResult& result){
        ImDrawList* dl = ImGui::GetWindowDrawList();
        ImVec2 origin = ImGui::GetWindowPos();
        float width = ImGui::GetWindowWidth();
        float height = px(72);
        float x = origin.x + px(24);
        if(mLogo){
            //The cat, in a rounded square as on othermythos.com.
            float logo = px(48);
            ImVec2 min(x, origin.y + (height - logo) * 0.5f);
            dl->AddImageRounded(ImTextureRef(mLogo), min, ImVec2(min.x + logo, min.y + logo), ImVec2(0, 0), ImVec2(1, 1), IM_COL32_WHITE, px(7));
            x += logo + px(14);
        }
        float big = px(28);
        drawText(dl, ImVec2(x, origin.y + (height - big) * 0.5f), big, Colours::kAccent, "OtherMythos");
        float titleWidth = textSize(big, "OtherMythos").x;
        drawText(dl, ImVec2(x + titleWidth + px(10), origin.y + (height - px(20)) * 0.5f + px(3)), px(20), Colours::kTextDim, "Launcher");

        //Always on screen, so there's an obvious way out with touch, mouse or controller (up from
        //the top of the project list).
        float quitHeight = px(46);
        std::string quitLabel = "    Quit";
        ImVec2 quitSize = buttonSize(quitLabel, px(120), quitHeight);
        ImVec2 quitPos(origin.x + width - px(24) - quitSize.x, origin.y + (height - quitHeight) * 0.5f);
        ImGui::SetCursorScreenPos(quitPos);
        //It's the first item in the window, so without this ImGui focuses it when the window
        //appears, rather than the first project.
        ImGui::PushItemFlag(ImGuiItemFlags_NoNavDefaultFocus, true);
        bool quit = ImGui::Button((quitLabel + "###quit").c_str(), quitSize);
        ImGui::PopItemFlag();
        if(quit){
            if(view.downloads.empty()) result.quit = true;
            else mOpenQuitRequest = true;
        }
        drawPowerIcon(dl, ImGui::GetItemRectMin(), ImGui::GetItemRectMax(), Colours::kText);
        if(mFocusQuitRequest){
            focusLastItem();
            mFocusQuitRequest = false;
        }
        if(ImGui::IsItemFocused()) focus.kind = FocusKind::QUIT;

        int ok = 0;
        int cached = 0;
        int64_t oldestCache = 0;
        for(const SourceStatus& s : view.sources){
            if(s.state == SourceState::OK) ok++;
            if(s.state == SourceState::CACHED){
                cached++;
                if(!oldestCache || s.fetchedAt < oldestCache) oldestCache = s.fetchedAt;
            }
        }
        std::string status;
        ImU32 dot = Colours::kGood;
        if(!view.httpProblem.empty()){
            status = "Offline: no network support";
            dot = Colours::kBad;
        }else if(view.refreshing){
            status = "Checking for builds";
            dot = Colours::kTextDim;
        }else if(view.sources.empty()){
            status = "No sources configured";
            dot = Colours::kBad;
        }else if(ok == int(view.sources.size())){
            status = "Online";
        }else{
            status = ok > 0 ? "Partly offline" : "Offline";
            dot = ok > 0 ? Colours::kAccent : Colours::kBad;
            if(cached > 0 && oldestCache > 0) status += ", builds as of " + formatAge(int64_t(time(nullptr)) - oldestCache);
        }
        float size = px(19);
        ImVec2 ts = textSize(size, status);
        float statusX = quitPos.x - px(28) - ts.x;
        float y = origin.y + (height - ts.y) * 0.5f;
        drawText(dl, ImVec2(statusX, y), size, Colours::kText, status);
        dl->AddCircleFilled(ImVec2(statusX - px(14), y + ts.y * 0.5f), px(6), dot, 16);
        ImGui::SetCursorPos(ImVec2(0, height));
        ImGui::Dummy(ImVec2(width, 0));
    }

    void Ui::drawProjects(const AppView& view, Focus& focus){
        if(view.projects.empty()){
            ImGui::PushStyleColor(ImGuiCol_Text, ImGui::ColorConvertU32ToFloat4(Colours::kTextDim));
            ImGui::TextWrapped("No projects yet.");
            ImGui::Spacing();
            if(view.refreshing) ImGui::TextWrapped("Checking the build indexes...");
            else ImGui::TextWrapped("Check the sources in the menu (Start).");
            ImGui::PopStyleColor();
            return;
        }
        ImDrawList* dl = ImGui::GetWindowDrawList();
        float rowHeight = px(76);
        for(size_t i = 0; i < view.projects.size(); i++){
            const CatalogProject& project = view.projects[i];
            bool selected = project.name == mSelectedProject;
            ImGui::PushID(project.name.c_str());
            bool clicked = ImGui::Selectable("##project", selected, ImGuiSelectableFlags_None, ImVec2(0, rowHeight));
            bool focused = ImGui::IsItemFocused();
            if(selected && mFocusProjectRequest){
                focusLastItem();
                mFocusProjectRequest = false;
            }
            //The panes share one navigation scope so left and right can cross between them, which
            //also let down from the last project drop into the build rows, where A plays a build.
            //Up and down stop at the ends of the list instead.
            bool first = i == 0;
            bool last = i + 1 == view.projects.size();
            if(focused && last && pressedDown()) focusLastItem();
            if(focused && first && pressedUp()) mFocusQuitRequest = true;
            ImVec2 min = ImGui::GetItemRectMin();
            ImVec2 max = ImGui::GetItemRectMax();
            if(selected) dl->AddRectFilled(min, ImVec2(min.x + px(5), max.y), Colours::kAccent, px(3));

            Featured f = featured(project);
            std::string state;
            ImU32 stateColour = Colours::kTextDim;
            const DownloadView* download = f.latest ? findDownload(view, project.name, f.latest->id, f.latestJob->name) : nullptr;
            if(!f.latest){
                state = "No " + view.platform + " builds";
            }else if(download){
                state = download->running ? "Downloading " + std::to_string(download->total ? int(download->done * 100 / download->total) : 0) + "%" : "Waiting to download";
                stateColour = Colours::kAccent;
            }else if(f.latestJob->installed){
                state = "Ready  ·  " + shortCommit(*f.latest);
                stateColour = Colours::kGood;
            }else if(f.installed){
                state = "Update available  ·  " + shortCommit(*f.latest);
                stateColour = Colours::kAccent;
            }else{
                state = "Not downloaded  ·  " + formatSize(f.latestJob->totalSize());
            }
            float x = min.x + px(20);
            drawText(dl, ImVec2(x, min.y + px(13)), px(24), Colours::kText, title(view, project.name));
            drawText(dl, ImVec2(x, min.y + px(45)), px(18), stateColour, state);

            if(focused){
                focus.kind = FocusKind::PROJECT;
                //Moving left out of the detail pane lands on whichever project is level with
                //the focused row; send it back to the selected one rather than switching.
                bool fromDetail = mLastFocusKind != FocusKind::PROJECT && mLastFocusKind != FocusKind::NONE;
                if(!selected && fromDetail) mFocusProjectRequest = true;
                else mSelectedProject = project.name;
            }
            if(clicked){
                mSelectedProject = project.name;
                mFocusPrimaryRequest = true;
            }
            ImGui::PopID();
        }
    }

    void Ui::activate(const AppView& view, const std::string& buildId, const std::string& job, UiResult& result){
        const CatalogProject* project = nullptr;
        for(const CatalogProject& p : view.projects) if(p.name == mSelectedProject) project = &p;
        if(!project) return;
        for(const CatalogBuild& b : project->builds){
            if(b.id != buildId) continue;
            for(const CatalogJob& j : b.jobs){
                if(j.name != job) continue;
                if(findDownload(view, project->name, buildId, job)) mApp.cancelDownload(project->name, buildId, job);
                else if(j.installed) result.launched = mApp.play(project->name, buildId, job);
                else mApp.download(project->name, buildId, job);
            }
        }
    }

    void Ui::drawDetail(const AppView& view, const CatalogProject& project, Focus& focus, UiResult& result){
        ImDrawList* dl = ImGui::GetWindowDrawList();
        Featured f = featured(project);

        ImGui::PushFont(nullptr, 34.0f);
        ImGui::TextUnformatted(title(view, project.name).c_str());
        ImGui::PopFont();
        ImGui::PushStyleColor(ImGuiCol_Text, ImGui::ColorConvertU32ToFloat4(Colours::kTextDim));
        if(f.latest){
            ImGui::Text("Latest  %s  ·  %s  ·  %s  ·  %s", shortCommit(*f.latest).c_str(), buildTime(*f.latest).c_str(),
                jobBuildType(f.latestJob->name).c_str(), formatSize(f.latestJob->totalSize()).c_str());
        }else{
            ImGui::Text("No builds for %s", view.platform.c_str());
        }
        ImGui::PopStyleColor();
        ImGui::Dummy(ImVec2(0, px(10)));

        //The primary action: play the newest build, download it, or both when an older one is installed.
        //The first button is always ##primary, so focus stays put when a download finishes and
        //Download turns into Play.
        if(f.latest){
            float buttonHeight = px(64);
            ImGui::PushFont(nullptr, 24.0f);
            const DownloadView* download = findDownload(view, project.name, f.latest->id, f.latestJob->name);
            bool requestFocus = mFocusPrimaryRequest;
            mFocusPrimaryRequest = false;
            bool hasPlay = f.latestJob->installed || f.installed;

            if(f.latestJob->installed || f.installed){
                const CatalogBuild* playBuild = f.latestJob->installed ? f.latest : f.installed;
                const CatalogJob* playJob = f.latestJob->installed ? f.latestJob : f.installedJob;
                std::string label = f.latestJob->installed ? "    Play" : "    Play " + shortCommit(*playBuild);
                ImGui::PushStyleColor(ImGuiCol_Button, ImGui::ColorConvertU32ToFloat4(IM_COL32(48, 120, 78, 255)));
                ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImGui::ColorConvertU32ToFloat4(IM_COL32(58, 140, 92, 255)));
                ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImGui::ColorConvertU32ToFloat4(IM_COL32(68, 160, 104, 255)));
                ImGui::PushStyleVar(ImGuiStyleVar_ButtonTextAlign, ImVec2(0.0f, 0.5f));
                if(ImGui::Button((label + "###primary").c_str(), buttonSize(label, px(240), buttonHeight))) result.launched = mApp.play(project.name, playBuild->id, playJob->name);
                if(requestFocus) focusLastItem();
                ImGui::PopStyleVar();
                ImGui::PopStyleColor(3);
                drawPlayIcon(dl, ImGui::GetItemRectMin(), ImGui::GetItemRectMax(), Colours::kText);
                if(ImGui::IsItemFocused()){
                    focus.kind = FocusKind::PLAY;
                    focus.buildId = playBuild->id;
                    focus.job = playJob->name;
                    focus.installed = true;
                    focus.updateAvailable = !f.latestJob->installed && !download;
                    focus.leftmost = true;
                }
                requestFocus = false;
                if(download || !f.latestJob->installed) ImGui::SameLine(0, px(16));
            }
            if(download){
                float fraction = download->total ? float(double(download->done) / double(download->total)) : 0.0f;
                ImGui::PushFont(nullptr, 19.0f);
                ImGui::ProgressBar(download->running ? fraction : 0.0f, ImVec2(px(420), buttonHeight), downloadLabel(*download).c_str());
                ImGui::PopFont();
                ImGui::SameLine(0, px(16));
                if(ImGui::Button(hasPlay ? "Cancel###secondary" : "Cancel###primary", buttonSize("Cancel", px(150), buttonHeight))) mApp.cancelDownload(project.name, f.latest->id, f.latestJob->name);
                if(requestFocus) focusLastItem();
                if(ImGui::IsItemFocused()){
                    focus.kind = FocusKind::CANCEL;
                    focus.buildId = f.latest->id;
                    focus.job = f.latestJob->name;
                    focus.downloading = true;
                    focus.leftmost = !hasPlay;
                }
            }else if(!f.latestJob->installed){
                std::string label = (f.installed ? "    Update  ·  " : "    Download  ·  ") + formatSize(f.latestJob->totalSize());
                ImGui::PushStyleVar(ImGuiStyleVar_ButtonTextAlign, ImVec2(0.0f, 0.5f));
                if(ImGui::Button((label + (hasPlay ? "###secondary" : "###primary")).c_str(), buttonSize(label, px(240), buttonHeight))){
                    mApp.download(project.name, f.latest->id, f.latestJob->name);
                }
                if(requestFocus) focusLastItem();
                ImGui::PopStyleVar();
                drawDownloadIcon(dl, ImGui::GetItemRectMin(), ImGui::GetItemRectMax(), Colours::kText);
                if(ImGui::IsItemFocused()){
                    focus.kind = f.installed ? FocusKind::UPDATE : FocusKind::DOWNLOAD;
                    focus.buildId = f.latest->id;
                    focus.job = f.latestJob->name;
                    focus.leftmost = !hasPlay;
                }
            }
            ImGui::PopFont();
        }

        auto run = view.lastRuns.find(project.name);
        if(run != view.lastRuns.end()){
            const RunRecord& r = run->second;
            ImGui::Dummy(ImVec2(0, px(4)));
            ImGui::PushStyleColor(ImGuiCol_Text, ImGui::ColorConvertU32ToFloat4(r.crashed ? Colours::kBad : Colours::kTextDim));
            ImGui::Text("Last played %s  ·  %s  ·  %s for %s", formatLocalShort(r.started).c_str(), r.commit.c_str(), r.description.c_str(),
                formatDuration(r.ended - r.started).c_str());
            ImGui::PopStyleColor();
        }

        ImGui::Dummy(ImVec2(0, px(20)));
        ImGui::PushStyleColor(ImGuiCol_Text, ImGui::ColorConvertU32ToFloat4(Colours::kTextDim));
        ImGui::TextUnformatted("All builds");
        ImGui::PopStyleColor();
        ImGui::Dummy(ImVec2(0, px(2)));

        float rowHeight = px(50);
        float size = px(20);
        for(const CatalogBuild& build : project.builds){
            for(const CatalogJob& job : build.jobs){
                ImGui::PushID((build.id + "/" + job.name).c_str());
                bool activated = ImGui::Selectable("##row", false, ImGuiSelectableFlags_None, ImVec2(0, rowHeight));
                bool focused = ImGui::IsItemFocused();
                ImVec2 min = ImGui::GetItemRectMin();
                ImVec2 max = ImGui::GetItemRectMax();
                if(focused) dl->AddRectFilled(min, max, IM_COL32(40, 52, 74, 255), px(8));
                float y = min.y + (rowHeight - size) * 0.5f;
                drawText(dl, ImVec2(min.x + px(16), y), size, Colours::kText, shortCommit(build));
                drawText(dl, ImVec2(min.x + px(130), y), size, Colours::kTextDim, buildTime(build));
                drawText(dl, ImVec2(min.x + px(290), y), size, Colours::kText, jobBuildType(job.name));
                drawText(dl, ImVec2(min.x + px(400), y), size, Colours::kTextDim, formatSize(job.totalSize()));

                const DownloadView* download = findDownload(view, project.name, build.id, job.name);
                std::string state;
                ImU32 colour = Colours::kTextDim;
                if(download){
                    state = download->running ? std::to_string(download->total ? int(download->done * 100 / download->total) : 0) + "%" : "Waiting";
                    colour = Colours::kAccent;
                    if(download->running && download->total){
                        float w = px(120);
                        ImVec2 barMin(max.x - px(16) - w, max.y - px(10));
                        dl->AddRectFilled(barMin, ImVec2(barMin.x + w, barMin.y + px(4)), IM_COL32(50, 60, 80, 255), px(2));
                        dl->AddRectFilled(barMin, ImVec2(barMin.x + w * float(double(download->done) / double(download->total)), barMin.y + px(4)), Colours::kAccent, px(2));
                    }
                }else if(job.installed){
                    state = "Installed";
                    colour = Colours::kGood;
                }else if(job.sources.empty()){
                    state = "Unavailable";
                }else{
                    state = "Download";
                }
                ImVec2 ts = textSize(size, state);
                drawText(dl, ImVec2(max.x - px(16) - ts.x, y - (download ? px(4) : 0)), size, colour, state);

                if(focused){
                    focus.kind = FocusKind::ROW;
                    focus.buildId = build.id;
                    focus.job = job.name;
                    focus.installed = job.installed;
                    focus.downloading = download != nullptr;
                    focus.leftmost = true;
                }
                if(activated) activate(view, build.id, job.name, result);
                ImGui::PopID();
            }
        }
    }

    void Ui::drawFooter(const AppView& view, const Focus& focus){
        ImDrawList* dl = ImGui::GetWindowDrawList();
        ImVec2 origin = ImGui::GetWindowPos();
        ImVec2 windowSize = ImGui::GetWindowSize();
        float height = px(56);
        float top = origin.y + windowSize.y - height;
        dl->AddRectFilled(ImVec2(origin.x, top), ImVec2(origin.x + windowSize.x, origin.y + windowSize.y), IM_COL32(16, 20, 28, 255));

        std::vector<Hint> hints;
        switch(focus.kind){
            case FocusKind::PROJECT:
                hints = {{"A", "Open"}, {"LB RB", "Project"}, {"View", "Refresh"}, {"Start", "Menu"}};
                break;
            case FocusKind::PLAY:
                hints.push_back({"A", "Play"});
                if(focus.updateAvailable) hints.push_back({"X", "Download update"});
                hints.push_back({"Y", "Delete"});
                hints.push_back({"B", "Back"});
                break;
            case FocusKind::UPDATE:
            case FocusKind::DOWNLOAD:
                hints = {{"A", "Download"}, {"B", "Back"}};
                break;
            case FocusKind::CANCEL:
                hints = {{"A", "Cancel download"}, {"B", "Back"}};
                break;
            case FocusKind::ROW:
                if(focus.downloading) hints = {{"A", "Cancel download"}, {"B", "Back"}};
                else if(focus.installed) hints = {{"A", "Play"}, {"Y", "Delete"}, {"B", "Back"}};
                else hints = {{"A", "Download"}, {"B", "Back"}};
                break;
            case FocusKind::QUIT:
                hints = {{"A", "Quit the launcher"}, {"B", "Back"}};
                break;
            case FocusKind::POPUP:
                hints = {{"A", "Select"}, {"B", "Close"}};
                break;
            case FocusKind::NONE:
                hints = {{"Start", "Menu"}};
                break;
        }

        float cy = top + height * 0.5f;
        float x = drawHints(dl, origin.x + px(24), cy, hints);

        std::string message = !view.configError.empty() ? view.configError : view.message;
        if(!message.empty()){
            ImU32 colour = (!view.configError.empty() || view.messageIsError) ? Colours::kBad : Colours::kTextDim;
            float right = origin.x + windowSize.x - px(24);
            float available = right - x;
            float size = px(17);
            //Clip long errors from the front of the line rather than run under the hints.
            std::string shown = message;
            while(shown.size() > 4 && textSize(size, shown).x > available) shown = shown.substr(0, shown.size() - 4) + "...";
            ImVec2 ts = textSize(size, shown);
            if(available > px(80)) drawText(dl, ImVec2(right - ts.x, cy - ts.y * 0.5f), size, colour, shown);
        }
    }

    void Ui::drawMenu(const AppView& view, bool fullscreen, UiResult& result){
        ImGuiViewport* viewport = ImGui::GetMainViewport();
        ImGui::SetNextWindowPos(viewport->GetCenter(), ImGuiCond_Always, ImVec2(0.5f, 0.5f));
        ImGui::SetNextWindowSize(ImVec2(std::min(px(760), viewport->Size.x - px(40)), 0));
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(px(28), px(24)));
        if(ImGui::BeginPopupModal("Menu", nullptr, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings)){
            ImGui::PushFont(nullptr, 28.0f);
            ImGui::TextUnformatted("Menu");
            ImGui::PopFont();
            ImGui::Dummy(ImVec2(0, px(6)));
            ImVec2 fullWidth(-FLT_MIN, px(52));
            if(ImGui::Button("Back", fullWidth)) ImGui::CloseCurrentPopup();
            if(ImGui::Button("Check for new builds", fullWidth)){
                mApp.refresh();
                ImGui::CloseCurrentPopup();
            }
            if(ImGui::Button(fullscreen ? "Leave fullscreen" : "Fullscreen", fullWidth)){
                result.toggleFullscreen = true;
                ImGui::CloseCurrentPopup();
            }
            if(ImGui::Button("Quit the launcher", fullWidth)){
                ImGui::CloseCurrentPopup();
                if(view.downloads.empty()) result.quit = true;
                else mOpenQuitRequest = true;
            }

            ImGui::Dummy(ImVec2(0, px(10)));
            ImGui::PushFont(nullptr, 17.0f);
            ImGui::TextDisabled("Sources");
            int64_t now = int64_t(time(nullptr));
            for(const SourceStatus& s : view.sources){
                ImVec4 colour = ImGui::ColorConvertU32ToFloat4(s.state == SourceState::OK ? Colours::kGood : s.state == SourceState::CACHED ? Colours::kAccent : Colours::kBad);
                std::string state = s.state == SourceState::OK ? "ok, checked " + formatAge(now - s.fetchedAt)
                    : s.state == SourceState::CACHED ? "offline, using its index from " + formatAge(now - s.fetchedAt)
                    : s.state == SourceState::FAILED ? "offline, and nothing saved from it" : "not checked yet";
                ImGui::TextColored(colour, "%s: %s", s.name.c_str(), state.c_str());
                ImGui::Indent(px(24));
                ImGui::PushStyleColor(ImGuiCol_Text, ImGui::ColorConvertU32ToFloat4(Colours::kTextDim));
                ImGui::TextWrapped("%s", s.url.c_str());
                ImGui::PopStyleColor();
                if(!s.error.empty() && s.state != SourceState::OK) ImGui::TextWrapped("%s", s.error.c_str());
                ImGui::Unindent(px(24));
            }
            ImGui::PushStyleColor(ImGuiCol_Text, ImGui::ColorConvertU32ToFloat4(Colours::kBad));
            if(!view.httpProblem.empty()) ImGui::TextWrapped("%s", view.httpProblem.c_str());
            if(!view.configError.empty()) ImGui::TextWrapped("%s", view.configError.c_str());
            ImGui::PopStyleColor();

            uint64_t used = 0;
            int installed = 0;
            for(const CatalogProject& p : view.projects){
                for(const CatalogBuild& b : p.builds){
                    for(const CatalogJob& j : b.jobs){
                        if(!j.installed) continue;
                        installed++;
                        used += j.totalSize();
                    }
                }
            }
            ImGui::Dummy(ImVec2(0, px(6)));
            ImGui::TextDisabled("Installed");
            ImGui::Text("%d builds, %s", installed, formatSize(used).c_str());
            ImGui::Indent(px(24));
            ImGui::PushStyleColor(ImGuiCol_Text, ImGui::ColorConvertU32ToFloat4(Colours::kTextDim));
            ImGui::TextWrapped("%s", view.dataDirectory.u8string().c_str());
            ImGui::PopStyleColor();
            ImGui::Unindent(px(24));
            ImGui::Dummy(ImVec2(0, px(6)));
            ImGui::TextDisabled("OtherMythos Launcher %s  ·  %s builds", kVersion, view.platform.c_str());
            ImGui::PopFont();

            if(ImGui::GetFrameCount() != mPopupOpenedFrame && (pressedBack() || pressedMenu())) ImGui::CloseCurrentPopup();
            ImGui::EndPopup();
        }
        ImGui::PopStyleVar();
    }

    void Ui::drawDeleteConfirm(const AppView& view){
        ImGuiViewport* viewport = ImGui::GetMainViewport();
        ImGui::SetNextWindowPos(viewport->GetCenter(), ImGuiCond_Always, ImVec2(0.5f, 0.5f));
        ImGui::SetNextWindowSize(ImVec2(std::min(px(560), viewport->Size.x - px(40)), 0));
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(px(28), px(24)));
        if(ImGui::BeginPopupModal("Delete", nullptr, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings)){
            const CatalogBuild* build = nullptr;
            const CatalogJob* job = nullptr;
            for(const CatalogProject& p : view.projects){
                if(p.name != mSelectedProject) continue;
                for(const CatalogBuild& b : p.builds){
                    for(const CatalogJob& j : b.jobs){
                        if(b.id == mDeleteBuildId && j.name == mDeleteJob){
                            build = &b;
                            job = &j;
                        }
                    }
                }
            }
            ImGui::PushFont(nullptr, 26.0f);
            ImGui::TextUnformatted("Delete this build?");
            ImGui::PopFont();
            if(build && job){
                ImGui::TextDisabled("%s  ·  %s  ·  %s  ·  %s", title(view, mSelectedProject).c_str(), shortCommit(*build).c_str(),
                    jobBuildType(job->name).c_str(), formatSize(job->totalSize()).c_str());
                if(job->sources.empty()) ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(Colours::kAccent), "No source has this build any more, so it can't be downloaded again.");
            }
            ImGui::Dummy(ImVec2(0, px(10)));
            ImVec2 size(px(200), px(52));
            if(ImGui::Button("Keep it", size)) ImGui::CloseCurrentPopup();
            ImGui::SetItemDefaultFocus();
            ImGui::SameLine(0, px(16));
            ImGui::PushStyleColor(ImGuiCol_Button, ImGui::ColorConvertU32ToFloat4(IM_COL32(150, 50, 45, 255)));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImGui::ColorConvertU32ToFloat4(IM_COL32(175, 60, 52, 255)));
            if(ImGui::Button("Delete", size)){
                if(build && job) mApp.deleteBuild(mSelectedProject, build->id, job->name);
                ImGui::CloseCurrentPopup();
            }
            ImGui::PopStyleColor(2);
            if(!build || !job || (ImGui::GetFrameCount() != mPopupOpenedFrame && pressedBack())) ImGui::CloseCurrentPopup();
            ImGui::EndPopup();
        }
        ImGui::PopStyleVar();
    }

    void Ui::drawQuitConfirm(UiResult& result){
        ImGuiViewport* viewport = ImGui::GetMainViewport();
        ImGui::SetNextWindowPos(viewport->GetCenter(), ImGuiCond_Always, ImVec2(0.5f, 0.5f));
        ImGui::SetNextWindowSize(ImVec2(std::min(px(600), viewport->Size.x - px(40)), 0));
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(px(28), px(24)));
        if(ImGui::BeginPopupModal("Quit", nullptr, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings)){
            ImGui::PushFont(nullptr, 26.0f);
            ImGui::TextUnformatted("Quit while downloading?");
            ImGui::PopFont();
            ImGui::TextDisabled("The download stops, and starts again from the beginning next time.");
            ImGui::Dummy(ImVec2(0, px(10)));
            ImVec2 size(px(240), px(52));
            if(ImGui::Button("Keep downloading", size)) ImGui::CloseCurrentPopup();
            ImGui::SetItemDefaultFocus();
            ImGui::SameLine(0, px(16));
            if(ImGui::Button("Quit", ImVec2(px(160), px(52)))) result.quit = true;
            if(ImGui::GetFrameCount() != mPopupOpenedFrame && pressedBack()) ImGui::CloseCurrentPopup();
            ImGui::EndPopup();
        }
        ImGui::PopStyleVar();
    }

    void Ui::handleButtons(const AppView& view, const Focus& focus){
        if(pressedMenu()){
            mOpenMenuRequest = true;
            return;
        }
        if(pressedRefresh()) mApp.refresh();
        if(!view.projects.empty() && (pressedPrevious() || pressedNext())){
            size_t index = 0;
            for(size_t i = 0; i < view.projects.size(); i++) if(view.projects[i].name == mSelectedProject) index = i;
            size_t count = view.projects.size();
            index = pressedNext() ? (index + 1) % count : (index + count - 1) % count;
            mSelectedProject = view.projects[index].name;
            mFocusProjectRequest = true;
            return;
        }
        //Left and right move between the panes here rather than through ImGui, whose directional
        //navigation only finds a target that's more beside the focus than above or below it, so
        //"left" from a build row low in the list found nothing.
        if(focus.kind == FocusKind::QUIT){
            if(pressedBack() || pressedDown() || pressedLeft()) mFocusProjectRequest = true;
            return;
        }
        bool inDetail = focus.kind != FocusKind::PROJECT && focus.kind != FocusKind::NONE;
        if(inDetail && (pressedBack() || (focus.leftmost && pressedLeft()))){
            mFocusProjectRequest = true;
            return;
        }
        if(focus.kind == FocusKind::PROJECT && pressedRight()){
            mFocusPrimaryRequest = true;
            return;
        }
        if(focus.buildId.empty()) return;
        if(pressedDownload() && (focus.updateAvailable || (!focus.installed && !focus.downloading))){
            if(focus.updateAvailable){
                for(const CatalogProject& p : view.projects){
                    if(p.name != mSelectedProject) continue;
                    Featured f = featured(p);
                    if(f.latest) mApp.download(p.name, f.latest->id, f.latestJob->name);
                }
            }else{
                mApp.download(mSelectedProject, focus.buildId, focus.job);
            }
        }
        if(pressedDelete() && focus.installed && !view.gameRunning){
            mDeleteBuildId = focus.buildId;
            mDeleteJob = focus.job;
            mOpenDeleteRequest = true;
        }
    }

    UiResult Ui::draw(const AppView& view, bool fullscreen){
        UiResult result;
        bool known = false;
        for(const CatalogProject& p : view.projects) if(p.name == mSelectedProject) known = true;
        if(!known && !view.projects.empty()){
            mSelectedProject = view.projects[0].name;
            mFocusProjectRequest = true;
        }

        ImGuiViewport* viewport = ImGui::GetMainViewport();
        ImGui::SetNextWindowPos(viewport->Pos);
        ImGui::SetNextWindowSize(viewport->Size);
        ImGuiWindowFlags flags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings
            | ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoScrollWithMouse;
        ImGui::Begin("##launcher", nullptr, flags);
        Focus focus;
        drawHeader(view, focus, result);

        float footerHeight = px(56);
        float gutter = px(20);
        float bodyTop = ImGui::GetCursorPosY();
        float bodyHeight = ImGui::GetWindowHeight() - bodyTop - footerHeight - gutter;
        float leftWidth = std::max(px(300), ImGui::GetWindowWidth() * 0.32f);
        ImGui::SetCursorPos(ImVec2(gutter, bodyTop));
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(px(10), px(10)));
        ImGui::BeginChild("projects", ImVec2(leftWidth, bodyHeight), ImGuiChildFlags_NavFlattened | ImGuiChildFlags_AlwaysUseWindowPadding);
        drawProjects(view, focus);
        ImGui::EndChild();
        ImGui::PopStyleVar();

        ImGui::SameLine(0, px(16));
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(px(28), px(22)));
        ImGui::BeginChild("detail", ImVec2(ImGui::GetWindowWidth() - ImGui::GetCursorPosX() - gutter, bodyHeight),
            ImGuiChildFlags_NavFlattened | ImGuiChildFlags_AlwaysUseWindowPadding);
        for(const CatalogProject& p : view.projects){
            if(p.name == mSelectedProject) drawDetail(view, p, focus, result);
        }
        ImGui::EndChild();
        ImGui::PopStyleVar();

        bool popupOpen = ImGui::IsPopupOpen("", ImGuiPopupFlags_AnyPopupId);
        Focus popupFocus;
        popupFocus.kind = FocusKind::POPUP;
        drawFooter(view, popupOpen ? popupFocus : focus);
        ImGui::End();

        if(!popupOpen){
            handleButtons(view, focus);
            mLastFocusKind = focus.kind;
        }
        if(ImGui::IsKeyPressed(ImGuiKey_F11, false)) result.toggleFullscreen = true;
        if(mOpenMenuRequest){
            ImGui::OpenPopup("Menu");
            mPopupOpenedFrame = ImGui::GetFrameCount();
            mOpenMenuRequest = false;
        }
        if(mOpenQuitRequest){
            ImGui::OpenPopup("Quit");
            mPopupOpenedFrame = ImGui::GetFrameCount();
            mOpenQuitRequest = false;
        }
        if(mOpenDeleteRequest){
            ImGui::OpenPopup("Delete");
            mPopupOpenedFrame = ImGui::GetFrameCount();
            mOpenDeleteRequest = false;
        }
        drawMenu(view, fullscreen, result);
        drawDeleteConfirm(view);
        drawQuitConfirm(result);
        return result;
    }
}
