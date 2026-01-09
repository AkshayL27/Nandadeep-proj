# Replace <server_ip> with your Server's IP
echo -n "REGISTER <NAME> 5000" | nc -u -w 1 <SERVER_IP> 5001
