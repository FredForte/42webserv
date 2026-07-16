import sys

def main():
    print("Greetings from Python CGI!!!")
    print("This script is: ", sys.argv[0])

    for i, arg in enumerate(sys.argv[1:], start=1):
        print(f"argv[{i}]: {arg}")

    print("Total number of arguments (not considering the script argument itself): ", len(sys.argv) - 1)

if __name__ == '__main__':
    main()
