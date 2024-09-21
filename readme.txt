Server: In implementarea serverului, am folosit cateve structuri din STL
        pentru a retine mai usor anumite corespondente intre date. Am folosit
        un map pentru a tine legatura dintre socketul de comunicare cu un client
        si id-ul sau. Mai avem nevoie de un set pentru a retine id-urile deja
        conectate la server (avem constrangerea de a nu exista mai multi clienti
        cu mai multe id-uri). Un alt set este folosit pentru a retine caror
        clienti inca nu le-am primit id-ul (id-ul fiecarui client il trimit
        ca primul pachet dupa ce s-a realizat conexiunea dintre client-server).
        Pentru ficare client, trebuie sa stim la ce topicuri este abonat si de
        aici apare folosirea unui alt map care sa faca corespondenta intre
        un id si topicurile abonate.

        Deoarece serverul asteapta date de la mai multi scoketi, avem nevoie de
        un poll prin care sa asteptam "in paralel" de la fiecare date. Pentru
        inceput avem socketii de listen pentru TCP si UDP (prin care clientii
        pot cere o conexiune cu serverul) si socketul pentru a citii date de la
        tastatura. Acesti 3 socketi vor ramane disponibili si nemodificati pana
        la inchiderea serverului. Pe parcursul programului, cand se stabilesc
        alte conexiuni, adaugam fiecare socket la poll.

Flow Server: Serverul trece prin toti socketii de pe care asteapta date si
             intampina urmatoarele variante:
             1) Avem o noua conexiune de la un subscriber TCP, unde se
             stabileste conexiune dintre client-server, iar pentru id
             pune clientul intr-o lista de asteptare si i se salveaza datele
             (ip & port).
             2) Avem input de la server, singurul mesaj acceptat este cel
             de exit, lucur pentru care parcurgem toti clientii si inchidem
             secvential conexiunile cu serverul.
             3) Avem date pe socketul de UDP, iar aici extragem datele
             din pachet si trimitem catre subscriberii topicului formatul
             corect al datelor.
             4) Avem date de la un client. Cum tcp garanteaza ordinea corecta
             a pachetelor, primul pachet va fi cel cu id-ul clientului. Putem
             verifica daca un pachet primit este primul, daca ne uitam in lista
             de asteptare. Daca socketul este pe lista de asteptare, verificam
             daca exista deja o conexiune cu id-ul primit (i.e. 2 clienti se
             conecteaza cu acelasi id), iar daca totul este corect, adaugam
             id-ul in lista de id-uri folosite, stergem din lista de asteptare
             socketul curent si adaugam o corespondenta intre socket si id.
             Altfel, acceptam doar comenzi de tipul subscribe/unsubscribe topic
             la care urmeaza algoritmul pentru implementarea wildcardsurilor.

Subscriber: In implementarea subscriberului avem 3 tipuri de evenimente, pentru
            care folosim 2 socketi, unul pentru comunicarea cu serverul
            (pentru comenzile de subscribe/unsubscribe/exit) si unul pentru
            citirea datelor de la tastatura. Subscriberul are 2 lucruri de
            facut, fie primeste un pachet de la server, caruia ii afiseaza
            continutul, fie primeste date de la tastatura si trimite mai
            departe catre server mesajul in sine (toate computatiile sunt
            facut de catre server) pus intr-un pachet.

Implementarea match-urilor intre wildcards:
        Fiecare id va avea intr-un vector, pe prima pozitie string-ul
        la care a dat subscribe, iar pe urmatoarele topicele delimitate de
        "/" (i.e. "ana/are/mere", "ana", "are", "mere"). Prima pozitie retine
        tot topicul, pentru ca la unsubscribe sa nu trebuiasca recompus tot
        topicul. Astfel, pentru un topic dat de udp si un client vom avea
        de comparat doi vectori de string-uri (i.e. {"ana", "are", "mere"},
        {"ana", "+", "*"}). Algoritmul presupune traversarea vectorilor in
        paralel, iar daca doua cuvinte sunt la fel, sau unul dintre ele
        este de tipul "+", se incrementeaza ambii iteratori din vectori.
        Cazul special este cel intalnit la "*", unde vom aplica recursiv
        functia pana la un rezultat pozitiv sau pana la finalul unuia dintre
        topice. De exemplu, daca avem la un moment dat {"*", "are"} si
        {"ceva", "are"}, vom aplica recursiv functia incepand de la urmatorul
        cuvant de dupa "*".
