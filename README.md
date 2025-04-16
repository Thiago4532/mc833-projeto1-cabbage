# Cabbage Movie Server

Sistema simples de gerenciamento de filmes via cliente-servidor, utilizando sockets TCP e comunicação binária estruturada.

```
Thiago Mota Martins                | RA: 223485
Lawrence Francisco Martins de Melo | RA: 223480
```

## Requisitos

Este projeto foi desenvolvido e testado em sistemas Linux.

### Programas necessários:
- `gcc`
- `make`
  
Para instalar os pacotes em um sistema baseado em Debian/Ubuntu:
```bash
sudo apt install build-essential
```

## Compilação

Na raiz do projeto, utilize o comando:

```bash
make all
```

Isso irá compilar tanto o cliente quanto o servidor:
- Cliente: `client/cabbage-client`
- Servidor: `server/cabbage-server`

Também é possível compilar separadamente:
```bash
make client
make server
```

Para limpar os arquivos gerados:
```bash
make clean
```

## Execução

### Servidor
Para iniciar o servidor na porta padrão (12345):
```bash
./server/cabbage-server
```

Ou especificar outra porta:
```bash
./server/cabbage-server 5000
```

O servidor armazenará os dados no arquivo de log `cabbage.log`.

### Cliente
Para conectar ao servidor:
```bash
./client/cabbage-client <ip_do_servidor> <porta>
```

Exemplo:
```bash
./client/cabbage-client 127.0.0.1 12345
```

## Comandos disponíveis no cliente

```bash
add "<title>" "<genres>" "<director>" "<year>"
  # Adiciona um novo filme. Exemplo:
  add "Inception" "Action,Sci-Fi" "Christopher Nolan" "2010"

list
  # Lista os filmes (apenas ID e título).

listd
  # Lista detalhada dos filmes.

get <movie_id>
  # Detalhes de um filme específico.

remove <movie_id>
  # Remove um filme.

addgenre <movie_id> <genre>
  # Adiciona um gênero a um filme.

listgenre <genre>
  # Lista filmes que possuem o gênero informado.

help
  # Mostra os comandos disponíveis.

quit | exit
  # Sai do cliente.
```
