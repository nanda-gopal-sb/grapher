#include <SFML/Graphics.hpp>
#include <iostream>
#include <vector>
struct node
{
    int x;
    int y;
    node(int _x, int _y)
    {
        x = _x;
        y = _y;
    }
};
std::vector<node *> Nodes;

bool checkClick(int x, int y)
{
    for (auto cell : Nodes)
    {
        if ((cell->x == x && cell->y == y))
        {
            return true;
        }
    }
    return false;
}
void drawCircle(sf::RenderWindow &window)
{
    sf::CircleShape dot(10.f);
    sf::VertexArray connect(sf::Lines, 2);
    dot.setFillColor(sf::Color::Blue);
    if (Nodes.size() >= 1)
    {
        for (int i = 0; i < Nodes.size() - 1; i++)
        {
            connect[0].position = sf::Vector2f(Nodes[i]->x + 10, Nodes[i]->y + 10);
            connect[1].position = sf::Vector2f(Nodes[i + 1]->x + 10, Nodes[i + 1]->y + 10);
            window.draw(connect);
        }
    }
    for (int i = 0; i < Nodes.size(); i++)
    {
        dot.setPosition(Nodes[i]->x, Nodes[i]->y);
        window.draw(dot);
    }
}
void destroy()
{
    for (auto cell : Nodes)
    {
        delete (cell);
    }
    Nodes.clear();
}
int main()
{
    auto window = sf::RenderWindow({1000u, 1000u}, "graphs");
    window.setFramerateLimit(144);

    while (window.isOpen())
    {
        for (auto event = sf::Event(); window.pollEvent(event);)
        {
            if (event.type == sf::Event::Closed)
            {
                destroy();
                window.close();
            }
            if (event.type == sf::Event::MouseButtonPressed &&
                event.mouseButton.button == sf::Mouse::Left)
            {

                int mouse_x = sf::Mouse::getPosition(window).x;
                int mouse_y = sf::Mouse::getPosition(window).y;
                node *newNode = new node(mouse_x, mouse_y);
                Nodes.push_back(newNode);
            }
            window.clear();
            drawCircle(window);
            window.display();
        }
    }
}
