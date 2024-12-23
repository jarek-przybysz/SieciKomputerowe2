#include <SFML/Graphics.hpp>
#include <iostream>
#include <string>
#include <thread>
#include <vector>
#include <map>
#include <cstring>
#include <arpa/inet.h>
#include <unistd.h>
#include "../json/include/nlohmann/json.hpp"


#define PORT 8080

using json = nlohmann::json;

// Funkcja do usuwania białych znaków z początku i końca ciągu znaków
std::string trim(const std::string &str)
{
    size_t first = str.find_first_not_of(" \t\r\n");
    size_t last = str.find_last_not_of(" \t\r\n");
    return (first == std::string::npos || last == std::string::npos) ? "" : str.substr(first, last - first + 1);
}

// Struktura wiadomości
struct GameMessage
{
    char playerName[50];
    int cardID;
    int tablecardid;
    char chosenSymbol[50];
    char cardSymbols[8][50];
    int score;
    int lobby;
};

// Struktura do przechowywania kart
struct Card
{
    int id;
    std::vector<std::string> symbols;
    bool isPlayerCard;
};

// Mapa do przechowywania tekstur kart
std::map<int, sf::Texture> cardTextures;

// Globalne zmienne
bool gameRunning = true;
bool inLobby = false;
bool gameEnded = false;
std::string winnerName;
int winnerScore = 0;
int score = 0; 
sf::Text winnerText;

// Funkcja do załadowania tekstur kart
void loadCardTextures()
{
    for (int i = 1; i <= 13; ++i)
    {
        sf::Texture texture;
        std::string filename = "images/card_id" + std::to_string(i) + ".png";
        if (texture.loadFromFile(filename))
        {
            texture.setSmooth(true);
            cardTextures[i] = texture;
        }
        else
        {
            std::cerr << "Nie udało się załadować obrazu " << filename << std::endl;
        }
    }
}

// Funkcja do wyświetlenia komunikatu o zwycięzcy
void displayWinnerMessage(const std::string &name, int score, sf::Font &font)
{
    winnerName = name;
    winnerScore = score;
    gameEnded = true;

    winnerText.setFont(font);
    winnerText.setString("Zwyciezca: " + winnerName + " z wynikiem " + std::to_string(winnerScore));
    winnerText.setCharacterSize(30);
    winnerText.setFillColor(sf::Color::Red);
    winnerText.setPosition(200, 300); // Środek ekranu
}

// Funkcja do ukrycia elementów gry
void hideGameElements(sf::Text &scoreText, sf::Text &symbolInputText, Card &playerCard, Card &tableCard)
{
    // Ukryj teksty związane z grą
    scoreText.setString("");
    symbolInputText.setString("");

    // Reset kart gracza i stołu
    playerCard.id = -1;
    tableCard.id = -1;
}

// Funkcja do odbierania wiadomości od serwera
void receiveMessages(int clientSocket, Card &playerCard, Card &tableCard, sf::Text &scoreText, sf::Font &font)
{

    GameMessage message;
    while (gameRunning)
    {
        int valread = recv(clientSocket, &message, sizeof(message), 0);
        if (valread <= 0)
        {
            std::cout << "Rozłączono z serwerem." << std::endl;
            gameRunning = false;
            break;
        }


        // Logowanie odebranych danych
        std::cout << "Odebrano dane od serwera:" << std::endl;
        std::cout << "  Player Name: " << message.playerName << std::endl;
        std::cout << "  Card ID: " << message.cardID << std::endl;
        std::cout << "  Table Card ID: " << message.tablecardid << std::endl;
        std::cout << "  Chosen Symbol: " << message.chosenSymbol << std::endl;

        // Sprawdź, czy gra się zakończyła (cardID == -1 oznacza koniec gry)
        if (message.tablecardid == -1)
        {
            displayWinnerMessage(message.playerName, message.score, font);
            hideGameElements(scoreText, scoreText, playerCard, tableCard);
            break;
        }

        // Obsługa wiadomości o karcie gracza
        playerCard.id = message.cardID;
        playerCard.symbols.clear();

        score = message.score; // Aktualizuj wynik
        scoreText.setString("Wynik: " + std::to_string(score));

        tableCard.id = message.tablecardid;
        std::cout << "Otrzymano kartę stołową: ID=" << tableCard.id;
        std::cout << std::endl;
    }
}

// Funkcja do wysyłania wiadomości z pełnymi informacjami o karcie gracza
void sendMessageWithCard(int clientSocket, const std::string &playerName, std::string &chosenSymbol, const Card &playerCard)
{
    GameMessage message;
    memset(&message, 0, sizeof(GameMessage)); // Wyzerowanie wiadomości
    strncpy(message.playerName, playerName.c_str(), sizeof(message.playerName) - 1);

    // Użycie funkcji `trim` na chosenSymbol przed wysłaniem
    std::string trimmedSymbol = trim(chosenSymbol);
    strncpy(message.chosenSymbol, trimmedSymbol.c_str(), sizeof(message.chosenSymbol) - 1);

    message.cardID = playerCard.id;

    // Wysłanie wiadomości do serwera i wyświetlenie w konsoli jako hex
    send(clientSocket, &message, sizeof(message), 0);
    std::cout << "Wysłany symbol: " << trimmedSymbol << std::endl;
}

// Funkcja główna klienta
int main(int argc, char *argv[])
{
    int clientSocket;
    struct sockaddr_in serverAddress;

    // Tworzenie gniazda
    if ((clientSocket = socket(AF_INET, SOCK_STREAM, 0)) < 0)
    {
        std::cerr << "Błąd tworzenia gniazda." << std::endl;
        return -1;
    }

    serverAddress.sin_family = AF_INET;
    serverAddress.sin_port = htons(PORT);

    // Przypisanie adresu IP serwera
    if (inet_pton(AF_INET, argv[2], &serverAddress.sin_addr) <= 0)
    {
        std::cerr << "Błąd adresu IP serwera." << std::endl;
        return -1;
    }

    // Połączenie z serwerem
    if (connect(clientSocket, (struct sockaddr *)&serverAddress, sizeof(serverAddress)) < 0)
    {
        std::cerr << "Połączenie nieudane." << std::endl;
        return -1;
    }

    // Tworzenie okna SFML
    sf::RenderWindow window(sf::VideoMode(800, 600), "Gra Dobble");
    window.setFramerateLimit(30);

    // Tworzenie czcionki
    sf::Font font;
    if (!font.loadFromFile("arial.ttf"))
    {
        std::cerr << "Nie udało się załadować czcionki." << std::endl;
        return -1;
    }
    // Załadowanie tekstur kart
    loadCardTextures();

    // Ekran wprowadzania imienia
    sf::Text enterNameText("Wprowadz swoja nazwe:", font, 24);
    enterNameText.setPosition(200, 200);
    enterNameText.setFillColor(sf::Color::Black); // Czarny kolor tekstu

    sf::Text nameInputText("", font, 24);
    nameInputText.setPosition(200, 250);
    nameInputText.setFillColor(sf::Color::Black); // Czarny kolor tekstu

    sf::Text okButton("OK", font, 24);
    okButton.setPosition(400, 300);
    okButton.setFillColor(sf::Color::Black); // Czarny kolor tekstu

    std::string playerName;
    GameMessage message;

    // Struktura kart
    Card playerCard, tableCard;

    // Tekst dla wpisywania symbolu
    sf::Text symbolInputText("Symbol: ", font, 20);
    symbolInputText.setPosition(200, 500);
    symbolInputText.setFillColor(sf::Color::Black); // Czarny kolor tekstu
    std::string symbolInput;

    // Tekst wyniku
    sf::Text scoreText("Wynik: 0", font, 20);
    scoreText.setPosition(20, 20);
    scoreText.setFillColor(sf::Color::Black); // Czarny kolor tekstu
    
    // Dodaj zmienną przycisku "Koniec"
    sf::Text endButton("Koniec", font, 24);
    endButton.setPosition(350, 400);
    endButton.setFillColor(sf::Color::Black); // Czarny kolor tekstu
    endButton.setStyle(sf::Text::Bold);

    // Wątek do odbierania wiadomości od serwera
    std::thread receiveThread(receiveMessages, clientSocket, std::ref(playerCard), std::ref(tableCard), std::ref(scoreText), std::ref(font));

    while (window.isOpen())
    {
        sf::Event event;
        while (window.pollEvent(event))
        {
            if (event.type == sf::Event::Closed)
            {
                gameRunning = false;
                window.close();
            }

            if (!inLobby)
            {
                if (event.type == sf::Event::TextEntered)
                {
                    if (event.text.unicode == '\b')
                    {
                        if (!playerName.empty())
                            playerName.pop_back();
                    }
                    else if (event.text.unicode < 128)
                    {
                        playerName += static_cast<char>(event.text.unicode);
                    }
                    nameInputText.setString(playerName);
                }

                // Zatwierdzenie wejścia do lobby przyciskiem OK lub klawiszem Enter
                else if ((event.type == sf::Event::MouseButtonPressed && okButton.getGlobalBounds().contains(event.mouseButton.x, event.mouseButton.y)) || (event.type == sf::Event::KeyPressed && event.key.code == sf::Keyboard::Enter))
                {
                    if (!playerName.empty())
                    { // Sprawdzenie, czy podano nazwę gracza
                        inLobby = true;
                        strncpy(message.playerName, playerName.c_str(), sizeof(message.playerName) - 1);
                        message.lobby = std::stoi(argv[1]);
                        std::cout << "Odebrano numer lobby: " << message.lobby << std::endl;
                        send(clientSocket, &message, sizeof(message), 0);
                        std::cout << "Wysłano wiadomość: Gracz " << playerName << " dołączył do gry" << std::endl;

                        // Ustawienie tytułu okna na "Gracz <nazwa gracza>"
                        window.setTitle("Gra gracza " + playerName);
                    }
                }
            }
            else if (!gameEnded)
            {
                if (event.type == sf::Event::TextEntered)
                {
                    if (event.text.unicode == '\b')
                    {
                        if (!symbolInput.empty())
                            symbolInput.pop_back();
                    }
                    else if (event.text.unicode < 128)
                    {
                        symbolInput += static_cast<char>(event.text.unicode);
                    }
                    symbolInputText.setString("Symbol: " + symbolInput);
                }
                else if (event.type == sf::Event::KeyPressed && event.key.code == sf::Keyboard::Enter)
                {
                    sendMessageWithCard(clientSocket, playerName, symbolInput, playerCard);
                    symbolInput.clear();
                }
            }
        }

        window.clear(sf::Color::White);

        if (!inLobby)
        {
            window.draw(enterNameText);
            window.draw(nameInputText);
            window.draw(okButton);
        }
        else if (!gameEnded)
        {
            if (cardTextures.find(playerCard.id) != cardTextures.end())
            {
                sf::Sprite playerCardSprite(cardTextures[playerCard.id]);
                playerCardSprite.setScale(0.15f, 0.15f);
                playerCardSprite.setPosition(450.f, 100.f);
                window.draw(playerCardSprite);
            }

            if (cardTextures.find(tableCard.id) != cardTextures.end())
            {
                sf::Sprite tableCardSprite(cardTextures[tableCard.id]);
                tableCardSprite.setScale(0.15f, 0.15f);
                tableCardSprite.setPosition(50.f, 100.f);
                window.draw(tableCardSprite);
            }

            window.draw(symbolInputText);
        }
        else
        {
            window.draw(winnerText); // Wyświetl komunikat o zwycięzcy
            window.draw(endButton);
            // Obsługa kliknięcia przycisku "Koniec"
            if (event.type == sf::Event::MouseButtonPressed && endButton.getGlobalBounds().contains(event.mouseButton.x, event.mouseButton.y))
            {
                gameRunning = false;
                window.close();
            }
        }

        window.display();
    }

    receiveThread.join();
    close(clientSocket);

    return 0;
}
