from pyhighs import linprog

if __name__ == '__main__':

    #linprog(['--model_file', 'ex1.mps', '--solver', 'simplex'])

    #linprog(model_file='ex1.mps', solver='simplex')
    linprog(model_file='ex1.mps', solver='ipm', run_quiet=True)
