gcc examples/crustlserver/main.c -I src/ -L target/debug/ -lcrustls -lpthread -ldl -o crustlserver && ./crustlserver localhost/cert.pem localhost/key.pem
