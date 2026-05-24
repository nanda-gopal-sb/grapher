// ─────────────────────────────────────────────────────────────────────────────
//  graph renderer  –  single file
//  usage: ./graph <directory>
//  compile: g++ -std=c++17 main.cpp -o graph -lsfml-graphics -lsfml-window -lsfml-system
// ─────────────────────────────────────────────────────────────────────────────

#include <SFML/Graphics.hpp>
#include <iostream>
#include <vector>
#include <string>
#include <fstream>
#include <sstream>
#include <filesystem>
#include <unordered_map>
#include <unordered_set>
#include <cmath>
#include <random>
#include <algorithm>

using AdjacencyList = std::unordered_map<std::string, std::vector<std::string>>;

// ═════════════════════════════════════════════════════════════════════════════
//  FileScanner  –  walks a directory, parses <a href> tags, builds the graph
// ═════════════════════════════════════════════════════════════════════════════
class FileScanner {
public:
    explicit FileScanner(const std::string& rootPath) : root_(rootPath) {}

    // scan everything and return a ready adjacency list
    AdjacencyList build() {
        collectFiles(root_);
        AdjacencyList adj;
        for (const auto& file : files_) {
            std::string content = readFile(file);
            if (content.empty()) continue;

            for (const auto& href : extractHrefs(content)) {
                // resolve href relative to the file that contains it
                std::string resolved = resolve(file, href);
                addEdge(adj, file, resolved);
            }
        }
        return adj;
    }

    // print all found files (debug)
    void printFiles() const {
        for (const auto& f : files_)
            std::cout << "  " << f << "\n";
    }

private:
    std::string              root_;
    std::vector<std::string> files_;

    // ── recursive directory walk ──────────────────────────────────────────
    void collectFiles(const std::string& path) {
        namespace fs = std::filesystem;
        if (!fs::exists(path) || !fs::is_directory(path)) return;

        for (const auto& entry : fs::directory_iterator(path)) {
            // skip hidden files / dirs
            if (entry.path().filename().string()[0] == '.') continue;

            if (fs::is_directory(entry.path()))
                collectFiles(entry.path().string());
            else
                files_.push_back(entry.path().string());
        }
    }

    // ── read entire file into string ──────────────────────────────────────
    static std::string readFile(const std::string& filename) {
        std::ifstream f(filename);
        if (!f.is_open()) {
            std::cerr << "Warning: could not open " << filename << "\n";
            return "";
        }
        std::ostringstream buf;
        buf << f.rdbuf();
        return buf.str();
    }

    // ── extract all href values from raw text ─────────────────────────────
    //    handles href="..." and href='...'  case-insensitively
    static std::vector<std::string> extractHrefs(const std::string& content) {
        std::vector<std::string> hrefs;
        size_t pos = 0;

        while (pos < content.size()) {
            // case-insensitive search for "href="
            size_t found = std::string::npos;
            for (size_t i = pos; i + 5 <= content.size(); ++i) {
                if (std::tolower(content[i])   == 'h' &&
                    std::tolower(content[i+1]) == 'r' &&
                    std::tolower(content[i+2]) == 'e' &&
                    std::tolower(content[i+3]) == 'f' &&
                    content[i+4] == '=')
                { found = i; break; }
            }
            if (found == std::string::npos) break;

            pos = found + 5; // skip past "href="
            if (pos >= content.size()) break;

            char quote = content[pos];
            if (quote != '"' && quote != '\'') continue; // malformed, skip

            size_t start = pos + 1;
            size_t end   = content.find(quote, start);
            if (end == std::string::npos) break;

            std::string href = content.substr(start, end - start);
            if (!href.empty()) hrefs.push_back(href);
            pos = end + 1;
        }
        return hrefs;
    }

    // ── resolve a relative href against its source file ───────────────────
    static std::string resolve(const std::string& sourceFile,
                               const std::string& href) {
        namespace fs = std::filesystem;
        // absolute or protocol hrefs stay as-is
        if (href.rfind("http", 0) == 0 || href.rfind("/", 0) == 0)
            return href;

        try {
            fs::path resolved = fs::path(sourceFile).parent_path() / href;
            return fs::weakly_canonical(resolved).string();
        } catch (...) {
            return href; // if resolution fails, keep raw
        }
    }

    // ── add directed edge, ensure both nodes exist as keys ───────────────
    static void addEdge(AdjacencyList& adj,
                        const std::string& u,
                        const std::string& v) {
        adj[u].push_back(v);
        if (adj.find(v) == adj.end())
            adj[v] = {}; // ensure isolated target nodes appear in the map
    }
};

// ═════════════════════════════════════════════════════════════════════════════
//  Vec2  –  minimal 2D vector
// ═════════════════════════════════════════════════════════════════════════════
struct Vec2 {
    float x = 0, y = 0;
    Vec2 operator+(const Vec2& o) const { return {x+o.x, y+o.y}; }
    Vec2 operator-(const Vec2& o) const { return {x-o.x, y-o.y}; }
    Vec2 operator*(float s)        const { return {x*s,   y*s};   }
    Vec2& operator+=(const Vec2& o){ x+=o.x; y+=o.y; return *this; }
    float length() const { return std::sqrt(x*x + y*y); }
    Vec2 normalized() const {
        float l = length();
        return l > 0.0001f ? Vec2{x/l, y/l} : Vec2{};
    }
};

// ═════════════════════════════════════════════════════════════════════════════
//  Node
// ═════════════════════════════════════════════════════════════════════════════
struct Node {
    std::string id;       // full canonical path
    std::string label;    // filename only, shown on screen
    Vec2        pos, vel, force;
    bool        exists  = true;   // false → dead link
    bool        pinned  = false;  // user-placed, skip simulation
    bool        hovered = false;
};

// ═════════════════════════════════════════════════════════════════════════════
//  GraphRenderer  –  Fruchterman-Reingold layout + SFML display
// ═════════════════════════════════════════════════════════════════════════════
struct GraphConfig {
    unsigned  windowW     = 1280;
    unsigned  windowH     = 800;
    float     nodeRadius  = 10.f;
    unsigned  fontSize    = 12u;
    // simulation
    float     idealLength = 120.f;
    float     repulsion   = 8000.f;
    float     damping     = 0.85f;
    float     maxSpeed    = 6.f;
    float     minTemp     = 0.01f;
    // colours
    sf::Color bg              = sf::Color(15,  15,  20);
    sf::Color edgeCol         = sf::Color(80,  80, 120, 160);
    sf::Color edgeHoverCol    = sf::Color(180, 220, 255, 220);
    sf::Color nodeCol         = sf::Color(80, 160, 255);
    sf::Color missingCol      = sf::Color(200,  60,  60);
    sf::Color pinnedCol       = sf::Color(255, 200,  80);
    sf::Color labelCol        = sf::Color(220, 220, 230);
};

class GraphRenderer {
public:
    using Config = GraphConfig;

    GraphRenderer(const AdjacencyList& adj, const Config& cfg = Config{})
        : cfg_(cfg)
    {
        buildGraph(adj);

        window_.create(sf::VideoMode(cfg_.windowW, cfg_.windowH),
                       "Graph Renderer", sf::Style::Default);
        window_.setFramerateLimit(60);

        // font – try common system paths
        fontOk_  = font_.loadFromFile("/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf")
                || font_.loadFromFile("/System/Library/Fonts/Helvetica.ttc")
                || font_.loadFromFile("C:/Windows/Fonts/arial.ttf");

        // random initial positions
        std::mt19937 rng(42);
        std::uniform_real_distribution<float> rx(100, cfg_.windowW  - 100);
        std::uniform_real_distribution<float> ry(100, cfg_.windowH - 100);
        for (auto& [id, n] : nodes_) n.pos = {rx(rng), ry(rng)};

        view_ = window_.getDefaultView();
    }

    void run() {
        while (window_.isOpen()) {
            handleEvents();
            if (temp_ > cfg_.minTemp) simulate();
            draw();
        }
    }

private:
    Config           cfg_;
    sf::RenderWindow window_;
    sf::Font         font_;
    bool             fontOk_ = false;
    sf::View         view_;

    std::unordered_map<std::string, Node>              nodes_;
    std::vector<std::pair<std::string, std::string>>   edges_;

    float temp_        = 1.0f;
    float coolingRate_ = 0.995f;

    // interaction
    std::string  draggedNode_;
    std::string  hoveredNode_;
    sf::Vector2f dragOffset_;
    bool         panning_  = false;
    sf::Vector2f panStart_;

    // ── build internal graph from adjacency list ──────────────────────────
    void buildGraph(const AdjacencyList& adj) {
        // register all unique node ids
        for (const auto& [u, neighbors] : adj) {
            ensureNode(u);
            for (const auto& v : neighbors) ensureNode(v);
        }

        // deduplicate edges (directed data, but drawn once per pair)
        std::unordered_set<std::string> seen;
        for (const auto& [u, neighbors] : adj) {
            for (const auto& v : neighbors) {
                std::string key = u < v ? u+"||"+v : v+"||"+u;
                if (seen.insert(key).second)
                    edges_.emplace_back(u, v);
            }
        }
    }

    void ensureNode(const std::string& id) {
        if (nodes_.count(id)) return;
        Node n;
        n.id     = id;
        n.label  = std::filesystem::path(id).filename().string();
        n.exists = std::filesystem::exists(id);
        nodes_[id] = n;
    }

    // ── Fruchterman-Reingold ──────────────────────────────────────────────
    void simulate() {
        float k = cfg_.idealLength;

        for (auto& [id, n] : nodes_) n.force = {};

        // repulsion – O(n²), fine for typical vault sizes
        std::vector<std::string> ids;
        for (const auto& [id, _] : nodes_) ids.push_back(id);

        for (size_t i = 0; i < ids.size(); ++i) {
            for (size_t j = i+1; j < ids.size(); ++j) {
                Node& a = nodes_[ids[i]];
                Node& b = nodes_[ids[j]];
                Vec2  d = a.pos - b.pos;
                float dist = std::max(d.length(), 0.1f);
                Vec2  f    = d.normalized() * (cfg_.repulsion / dist);
                a.force += f;
                b.force += {-f.x, -f.y};
            }
        }

        // attraction – spring along each edge
        for (const auto& [u, v] : edges_) {
            if (!nodes_.count(u) || !nodes_.count(v)) continue;
            Node& a = nodes_[u];
            Node& b = nodes_[v];
            Vec2  d = b.pos - a.pos;
            float dist = std::max(d.length(), 0.1f);
            Vec2  f    = d.normalized() * (dist * dist / k);
            a.force += f;
            b.force += {-f.x, -f.y};
        }

        // gentle gravity toward center
        Vec2 center = {cfg_.windowW * 0.5f, cfg_.windowH * 0.5f};
        for (auto& [id, n] : nodes_)
            n.force += (center - n.pos) * 0.003f;

        // integrate
        float cap = temp_ * cfg_.maxSpeed * 10.f;
        for (auto& [id, n] : nodes_) {
            if (n.pinned) continue;
            n.vel = (n.vel + n.force) * cfg_.damping;
            if (n.vel.length() > cap) n.vel = n.vel.normalized() * cap;
            n.pos += n.vel;
            n.pos.x = std::clamp(n.pos.x, cfg_.nodeRadius, (float)cfg_.windowW  - cfg_.nodeRadius);
            n.pos.y = std::clamp(n.pos.y, cfg_.nodeRadius, (float)cfg_.windowH - cfg_.nodeRadius);
        }

        temp_ *= coolingRate_;
    }

    // ── events ────────────────────────────────────────────────────────────
    void handleEvents() {
        sf::Event ev;
        while (window_.pollEvent(ev)) {

            if (ev.type == sf::Event::Closed)
                window_.close();

            // zoom
            if (ev.type == sf::Event::MouseWheelScrolled) {
                view_.zoom(ev.mouseWheelScroll.delta > 0 ? 0.9f : 1.1f);
                window_.setView(view_);
            }

            if (ev.type == sf::Event::MouseButtonPressed) {
                sf::Vector2f wp = toWorld({ev.mouseButton.x, ev.mouseButton.y});
                std::string  hit = nodeAt(wp);

                if (ev.mouseButton.button == sf::Mouse::Left) {
                    if (!hit.empty()) {
                        draggedNode_ = hit;
                        dragOffset_  = {wp.x - nodes_[hit].pos.x,
                                        wp.y - nodes_[hit].pos.y};
                    } else {
                        panning_  = true;
                        panStart_ = {(float)ev.mouseButton.x,
                                     (float)ev.mouseButton.y};
                    }
                }
                // right click: toggle pin
                if (ev.mouseButton.button == sf::Mouse::Right && !hit.empty()) {
                    nodes_[hit].pinned = !nodes_[hit].pinned;
                    nodes_[hit].vel    = {};
                }
            }

            if (ev.type == sf::Event::MouseButtonReleased) {
                if (!draggedNode_.empty()) {
                    nodes_[draggedNode_].pinned = true;
                    nodes_[draggedNode_].vel    = {};
                    draggedNode_.clear();
                }
                panning_ = false;
            }

            if (ev.type == sf::Event::MouseMoved) {
                sf::Vector2f wp = toWorld({ev.mouseMove.x, ev.mouseMove.y});

                if (!draggedNode_.empty())
                    nodes_[draggedNode_].pos = {wp.x - dragOffset_.x,
                                                wp.y - dragOffset_.y};

                if (panning_) {
                    sf::Vector2f d = { panStart_.x - ev.mouseMove.x,
                                       panStart_.y - ev.mouseMove.y };
                    view_.move(d * (view_.getSize().x / cfg_.windowW));
                    panStart_ = {(float)ev.mouseMove.x, (float)ev.mouseMove.y};
                    window_.setView(view_);
                }

                hoveredNode_ = nodeAt(wp);
                for (auto& [id, n] : nodes_)
                    n.hovered = (id == hoveredNode_);
            }

            if (ev.type == sf::Event::KeyPressed) {
                // Space: reheat
                if (ev.key.code == sf::Keyboard::Space) {
                    temp_ = 1.0f;
                    for (auto& [id, n] : nodes_) { n.pinned = false; n.vel = {}; }
                }
                // R: reset camera
                if (ev.key.code == sf::Keyboard::R) {
                    view_ = window_.getDefaultView();
                    window_.setView(view_);
                }
            }
        }
    }

    // ── drawing ───────────────────────────────────────────────────────────
    void draw() {
        window_.clear(cfg_.bg);

        // edges
        for (const auto& [u, v] : edges_) {
            if (!nodes_.count(u) || !nodes_.count(v)) continue;
            const Node& a   = nodes_.at(u);
            const Node& b   = nodes_.at(v);
            bool  hi        = a.hovered || b.hovered;
            sf::Color col   = hi ? cfg_.edgeHoverCol : cfg_.edgeCol;

            sf::Vertex line[2] = {
                sf::Vertex({a.pos.x, a.pos.y}, col),
                sf::Vertex({b.pos.x, b.pos.y}, col)
            };
            window_.draw(line, 2, sf::Lines);
            drawArrow(a.pos, b.pos, col);
        }

        // nodes
        for (const auto& [id, n] : nodes_) {
            sf::Color fill = n.pinned  ? cfg_.pinnedCol
                           : !n.exists ? cfg_.missingCol
                                       : cfg_.nodeCol;
            float r = cfg_.nodeRadius;

            if (n.hovered) {
                fill = brighten(fill, 60);
                r   *= 1.3f;
            }

            // glow
            sf::CircleShape glow(r * 1.8f);
            glow.setOrigin(r*1.8f, r*1.8f);
            glow.setPosition(n.pos.x, n.pos.y);
            glow.setFillColor({fill.r, fill.g, fill.b, 35});
            window_.draw(glow);

            // circle
            sf::CircleShape circle(r);
            circle.setOrigin(r, r);
            circle.setPosition(n.pos.x, n.pos.y);
            circle.setFillColor(fill);
            circle.setOutlineColor({255,255,255,50});
            circle.setOutlineThickness(1.f);
            window_.draw(circle);

            // label
            if (fontOk_) {
                sf::Text lbl;
                lbl.setFont(font_);
                lbl.setString(n.label);
                lbl.setCharacterSize(cfg_.fontSize);
                lbl.setFillColor(cfg_.labelCol);
                auto b = lbl.getLocalBounds();
                lbl.setOrigin(b.width / 2.f, 0);
                lbl.setPosition(n.pos.x, n.pos.y + cfg_.nodeRadius + 4.f);
                window_.draw(lbl);
            }
        }

        // HUD (always in screen space)
        if (fontOk_) {
            sf::Text hud;
            hud.setFont(font_);
            hud.setCharacterSize(11);
            hud.setFillColor({140, 140, 160});
            hud.setString("Drag: move  |  RClick: pin  |  Scroll: zoom  "
                          "|  Space: reheat  |  R: reset view");
            hud.setPosition(10, cfg_.windowH - 22);
            window_.setView(window_.getDefaultView());
            window_.draw(hud);
            window_.setView(view_);
        }

        window_.display();
    }

    void drawArrow(Vec2 from, Vec2 to, sf::Color col) {
        Vec2  dir  = (to - from).normalized();
        float r    = cfg_.nodeRadius;
        Vec2  tip  = {to.x - dir.x*r, to.y - dir.y*r};
        Vec2  perp = {-dir.y, dir.x};
        float sz   = 7.f;

        sf::ConvexShape arrow;
        arrow.setPointCount(3);
        arrow.setPoint(0, {tip.x, tip.y});
        arrow.setPoint(1, {tip.x - dir.x*sz + perp.x*sz*0.5f,
                           tip.y - dir.y*sz + perp.y*sz*0.5f});
        arrow.setPoint(2, {tip.x - dir.x*sz - perp.x*sz*0.5f,
                           tip.y - dir.y*sz - perp.y*sz*0.5f});
        arrow.setFillColor(col);
        window_.draw(arrow);
    }

    // ── helpers ───────────────────────────────────────────────────────────
    sf::Vector2f toWorld(sf::Vector2i pixel) const {
        return window_.mapPixelToCoords(pixel, view_);
    }

    std::string nodeAt(sf::Vector2f wp) const {
        float r2 = cfg_.nodeRadius * cfg_.nodeRadius * 2.5f;
        for (const auto& [id, n] : nodes_) {
            float dx = wp.x - n.pos.x, dy = wp.y - n.pos.y;
            if (dx*dx + dy*dy <= r2) return id;
        }
        return "";
    }

    static sf::Color brighten(sf::Color c, int amt) {
        return { (sf::Uint8)std::min(255, c.r+amt),
                 (sf::Uint8)std::min(255, c.g+amt),
                 (sf::Uint8)std::min(255, c.b+amt) };
    }
};

// ═════════════════════════════════════════════════════════════════════════════
//  main
// ═════════════════════════════════════════════════════════════════════════════
int main(int argc, char* argv[]) {
    if (argc < 2) {
        std::cerr << "Usage: " << argv[0] << " <directory>\n";
        return 1;
    }

    std::cout << "Scanning " << argv[1] << " ...\n";
    FileScanner scanner(argv[1]);
    AdjacencyList adj = scanner.build();

    std::cout << "Found " << adj.size() << " nodes\n";
    for (const auto& [node, neighbors] : adj) {
        std::cout << "  " << node << ": ";
        for (const auto& n : neighbors) std::cout << n << " ";
        std::cout << "\n";
    }

    GraphRenderer renderer(adj);
    renderer.run();
    return 0;
}