import seaborn as sns
import pandas as pd
import numpy as np
import matplotlib.pyplot as plt
from matplotlib.patches import Polygon
import os
from statistics import mean

# set style
plt.style.use('/mutable/paper/.matplotlib/matplotlibrc')

DATA = os.path.join('../eval.out')
COLUMN_TYPES = { 'benchmark': str, 'experiment': str, 'name': str, 'config': str, 'case': str, 'time': np.double, 'runid': np.intc }
KEYS = ['name', 'config', 'case']
BENCHMARK_NAMES = {'tpch': 'TPC-H', 'job': 'JOB'}

def set_size(fraction_width=0.95, fraction_height=0.25):
    width_pt = 241.14749 # column width in pt
    height_pt = 626.0 # page height in pt

    fig_width_pt = width_pt * fraction_width
    fig_height_pt = height_pt * fraction_height
    inches_per_pt = 1 / 72.27

    fig_width_in = fig_width_pt * inches_per_pt
    fig_height_in = fig_height_pt * inches_per_pt

    return (fig_width_in, fig_height_in)

def get_data(benchmark_name, experiment_name, data):
    data = data[(data['benchmark'] == benchmark_name) & (data['experiment'] == experiment_name)]
    data = data.groupby(KEYS)['time'].median().reset_index()
    data.rename(columns={'name': 'Configuration', 'config': 'Measurement'}, inplace=True)
    return data

def plot_solution_space():
    split = (0.15,1)
    holistic = (1.5,0.15)
    pareto = (0.15,0.15)

    # create plot
    plt.figure(figsize=set_size(0.7, 0.14), layout='constrained')
    plt.xlim(0, 2)
    plt.ylim(0, 1.2)

    # add split, holistic, and pareto-optimum
    plt.gca().scatter(split[0], split[1], marker='x', color='#7851A9')
    plt.text(split[0] + .03, split[1] + .06, s='\\textsc{split}', ha='left', va='center', color='#7851A9')
    plt.gca().scatter(holistic[0], holistic[1], marker='x', color='#82ad2a')
    plt.text(holistic[0] - .02, holistic[1] - .06, s='\\textsc{holistic}', ha='right', va='center', color='#82ad2a')
    plt.gca().scatter(pareto[0], pareto[1], marker='x', color='black')
    plt.text(pareto[0] + .03, pareto[1] + .05, s='Pareto', ha='left', va='center')
    plt.text(pareto[0] + .03, pareto[1] - .05, s='Optimum', ha='left', va='center')

    # add top-k
    topk = Polygon([split, (0.5,1), (1.9,0.15), (0.3,0.15)], color='#00CED1', alpha=.5, edgecolor=None)
    plt.gca().add_patch(topk)
    plt.text(0.5, 0.58, s='\\textsc{top-k}')
    plt.text(0.5, 0.48, s='(ours)')
    overhead = Polygon([holistic, (1.9,0.15), (holistic[0],0.40)], fill=None, alpha=.5, edgecolor='black', linestyle='dashed', hatch='////')
    plt.gca().add_patch(overhead)
    plt.text(1.45, 0.58, s='Overhead compared')
    plt.text(1.45, 0.48, s='to \\textsc{holistic}')
    plt.plot([1.7, 1.62], [0.45, 0.23], '-', color='black')

    # adapt style
    plt.xlabel('Optimization Time')
    plt.ylabel('Estimated\nPhysical Cost')
    plt.xticks([])
    plt.yticks([0.15, 1], labels=['Global Optimum', 'Local Optimum'])
    plt.gca().yaxis.tick_right()
    plt.gca().tick_params(length=0, grid_color='black', grid_alpha=.5, pad=3)
    plt.gca().spines[['right', 'top']].set_visible(False)
    plt.gca().plot(1, 0, ">k", transform=plt.gca().get_yaxis_transform(), clip_on=False)
    plt.gca().plot(0, 1.2, "^k", transform=plt.gca().get_yaxis_transform(), clip_on=False)
    plt.grid(axis='y', linestyle='dashed')

    plt.savefig(f"fig/solution_space.pdf", transparent=True, bbox_inches=None)

def plot_optimization(data):
    def preprocess_data(query_graph_shape):
        df = get_data('optimization time', query_graph_shape, data)
        df['case'] = pd.to_numeric(df['case'])

        df_holistic = df[(df['Configuration'] == 'holistic (DPccp)') | (df['Configuration'] == 'holistic (PEall)')]
        df_split = df[(df['Configuration'] == 'split (DPccp)') | (df['Configuration'] == 'split (PEall)')]
        df_top5 = df[(df['Configuration'] == 'top-5 (DPccp)') | (df['Configuration'] == 'top-5 (PEall)')]
        df_top10 = df[(df['Configuration'] == 'top-10 (DPccp)') | (df['Configuration'] == 'top-10 (PEall)')]

        # add algebraic and physical optimization time for split approach
        pivot_df = df_split.pivot(index=['Configuration', 'case'], columns='Measurement', values='time')
        pivot_df['Optimization Time'] = pivot_df['Optimization Time (algebraic)'] + pivot_df['Optimization Time (physical)']
        df_split = pivot_df.reset_index().melt(id_vars=['Configuration', 'case'], var_name='Measurement', value_name='time').dropna()
        df_split = df_split[df_split['Measurement'] == 'Optimization Time']

        # add algebraic and physical optimization time for topk approach
        pivot_df = df_top5.pivot(index=['Configuration', 'case'], columns='Measurement', values='time')
        pivot_df['Optimization Time'] = pivot_df['Optimization Time (algebraic)'] + pivot_df['Optimization Time (physical)']
        df_top5 = pivot_df.reset_index().melt(id_vars=['Configuration', 'case'], var_name='Measurement', value_name='time').dropna()
        df_top5 = df_top5[df_top5['Measurement'] == 'Optimization Time']
        pivot_df = df_top10.pivot(index=['Configuration', 'case'], columns='Measurement', values='time')
        pivot_df['Optimization Time'] = pivot_df['Optimization Time (algebraic)'] + pivot_df['Optimization Time (physical)']
        df_top10 = pivot_df.reset_index().melt(id_vars=['Configuration', 'case'], var_name='Measurement', value_name='time').dropna()
        df_top10 = df_top10[df_top10['Measurement'] == 'Optimization Time']

        df = pd.concat([df_holistic, df_split, df_top5, df_top10])
        configuration_order = ['split (DPccp)', 'split (PEall)', 'top-5 (DPccp)', 'top-5 (PEall)', 'top-10 (DPccp)', 'top-10 (PEall)', 'holistic (DPccp)', 'holistic (PEall)']
        df['Configuration'] = pd.Categorical(df['Configuration'], categories=configuration_order, ordered=True)
        df = df.sort_values(by='Configuration')

        df.insert(0, "Shape", query_graph_shape.capitalize())
        return df

    df = pd.concat([preprocess_data('chain'), preprocess_data('cycle'), preprocess_data('star'), preprocess_data('clique')])

    # create a 2x2 grid of subplots
    fig, axes = plt.subplots(2, 2, figsize=set_size(2.0, 0.4), layout='constrained', sharex=True, sharey=True)

    # define the plot groups
    plot_groups = df['Shape'].unique()

    # loop through the plot groups and plot each group in a subplot
    for i, group in enumerate(plot_groups):
        # select the subplot
        ax = axes[i // 2, i % 2]

        # filter data for the current plot group
        data_group = df[df['Shape'] == group]

        # create a grouped bar plot
        sns.barplot(x='case', y='time', hue='Configuration', data=data_group, ax=ax, palette=['tab:blue', 'tab:blue', 'tab:olive', 'tab:olive', 'tab:green', 'tab:green', 'tab:orange', 'tab:orange'], zorder=3, edgecolor='black')
        for j, bar in enumerate(ax.patches):
            if (j // len(data_group['case'].unique())) % 2 == 1:
                bar.set_hatch('////')
        ax.set_yscale('log')

        # adapt style
        ax.set_title(group)
        ax.set_xlabel(None)
        ax.set_ylabel(None)
        handles, labels = ax.get_legend_handles_labels()
        ax.legend().remove()
        ax.grid(axis='y', zorder=0)

    # adapt common style
    fig.supxlabel('\#Relations', x=0.53)
    fig.supylabel('Query Optimization Time [ms] (log-scale)', y=0.54)
    handles[1].set_hatch('////')
    handles[3].set_hatch('////')
    handles[5].set_hatch('////')
    handles[7].set_hatch('////')
    for i in range(len(labels)):
        labels[i] = labels[i].replace('split', '\\textsc{split}')
        labels[i] = labels[i].replace('top-5', '\\textsc{top-5}')
        labels[i] = labels[i].replace('top-10', '\\textsc{top-10}')
        labels[i] = labels[i].replace('holistic', '\\textsc{holistic}')
        labels[i] = labels[i].replace('DPccp', '$\\textit{DP}_\\textit{ccp}$')
        labels[i] = labels[i].replace('PEall', '$\\textit{PE}_\\textit{all}$')
    fig.legend(handles, labels, frameon=False, ncols=4, bbox_to_anchor=(0.83, 1.11))

    plt.savefig('fig/optimization.pdf', transparent=True, bbox_inches='tight')

def plot_optimization_split(data):
    def preprocess_data(query_graph_shape):
        df = get_data('optimization time', query_graph_shape, data)
        df['case'] = pd.to_numeric(df['case'])

        df = df[(df['Configuration'] == 'split (DPccp)') | (df['Configuration'] == 'split (PEall)')]

        # add algebraic to physical optimization time such that plots appear to be stacked
        pivot_df = df.pivot(index=['Configuration', 'case'], columns='Measurement', values='time')
        pivot_df['Optimization Time (physical)'] = pivot_df['Optimization Time (algebraic)'] + pivot_df['Optimization Time (physical)']
        df = pivot_df.reset_index().melt(id_vars=['Configuration', 'case'], var_name='Measurement', value_name='time').dropna()

        configuration_order = ['split (DPccp)', 'split (PEall)']
        df['Configuration'] = pd.Categorical(df['Configuration'], categories=configuration_order, ordered=True)
        df = df.sort_values(by='Configuration')

        df.insert(0, "Shape", query_graph_shape.capitalize())
        return df

    df = pd.concat([preprocess_data('chain'), preprocess_data('clique')])

    # create a 1x2 grid of subplots
    fig, axes = plt.subplots(1, 2, figsize=set_size(2.0, 0.22), layout='constrained', sharex=True, sharey=True)

    # define the plot groups
    plot_groups = df['Shape'].unique()

    # loop through the plot groups and plot each group in a subplot
    for i, group in enumerate(plot_groups):
        # select the subplot
        ax = axes[i]

        # filter data for the current plot group
        data_group = df[df['Shape'] == group]

        # create a stacked grouped bar plot
        data_algebraic = data_group[data_group['Measurement'] == 'Optimization Time (algebraic)']
        data_physical = data_group[data_group['Measurement'] == 'Optimization Time (physical)']
        data_algebraic['Configuration'] = data_algebraic['Configuration'].str.replace('split', 'algebraic')
        data_physical['Configuration'] = data_physical['Configuration'].str.replace('split', 'physical')
        sns.barplot(x='case', y='time', hue='Configuration', data=data_algebraic, ax=ax, palette=['tab:blue'], zorder=4, edgecolor='black')
        sns.barplot(x='case', y='time', hue='Configuration', data=data_physical, ax=ax, palette=[sns.color_palette('pastel')[0]], zorder=3, edgecolor='black')
        for j, bar in enumerate(ax.patches):
            if j in [9,10,11,12,13,14,15,16,17,29,30,31,32,33,34,35,36,37]:
                bar.set_hatch('////')

        # adapt style
        ax.set_title(group, fontweight='bold')
        ax.set_xlabel(None)
        ax.set_ylabel(None)
        handles, labels = ax.get_legend_handles_labels()
        ax.legend().remove()
        ax.grid(axis='y', zorder=0)

    # adapt common style
    fig.supxlabel('\#Relations', x=0.525)
    fig.supylabel('Query Optimization Time [ms]', y=0.6)
    handles[1], handles[2] = handles[2], handles[1]
    labels[1], labels[2] = labels[2], labels[1]
    handles[2].set_hatch('////')
    handles[3].set_hatch('////')
    for i in range(len(labels)):
        labels[i] = labels[i].replace('DPccp', '$\\textit{DP}_\\textit{ccp}$')
        labels[i] = labels[i].replace('PEall', '$\\textit{PE}_\\textit{all}$')
    fig.legend(handles, labels, frameon=False, ncols=4, bbox_to_anchor=(0.85, 1.13))

    plt.savefig('fig/optimization_split.pdf', transparent=True, bbox_inches='tight')

def plot_microbenchmark(data, name, minimal_sf=None, except_sf=[], title=None):
    df = get_data('microbenchmark', name, data)
    df['case'] = pd.to_numeric(df['case'])

    if minimal_sf:
        df = df[df['case'] >= minimal_sf]
    for sf in except_sf:
        df = df[df['case'] != sf]

    df_holistic = df[(df['Configuration'] == 'holistic (DPccp)') | (df['Configuration'] == 'holistic (PEall)')]
    df_split = df[(df['Configuration'] == 'split (DPccp)') | (df['Configuration'] == 'split (PEall)')]

    # add algebraic and physical optimization time for split and top-k approach
    pivot_df = df_split.pivot(index=['Configuration', 'case'], columns='Measurement', values='time')
    pivot_df['Optimization Time'] = pivot_df['Optimization Time (algebraic)'] + pivot_df['Optimization Time (physical)']
    df_split = pivot_df.reset_index().melt(id_vars=['Configuration', 'case'], var_name='Measurement',
                                           value_name='time').dropna()
    df_split = df_split[
        (df_split['Measurement'] == 'Optimization Time') | (df_split['Measurement'] == 'Execution Time')]

    df = pd.concat([df_holistic, df_split])

    # add optimization to running time such that plots appear to be stacked
    pivot_df = df.pivot(index=['Configuration', 'case'], columns='Measurement', values='time')
    pivot_df['Execution Time'] = pivot_df['Optimization Time'] + pivot_df['Execution Time']
    df = pivot_df.reset_index().melt(id_vars=['Configuration', 'case'], var_name='Measurement',
                                     value_name='time').dropna()

    configuration_order = ['split (DPccp)', 'split (PEall)', 'holistic (DPccp)', 'holistic (PEall)']
    df['Configuration'] = pd.Categorical(df['Configuration'], categories=configuration_order, ordered=True)
    df = df.sort_values(by='Configuration')

    # create a plot
    plt.figure(figsize=set_size(1.0, 0.218), layout='constrained')

    # define the configurations
    configurations = df['Configuration'].unique()

    # create a stacked grouped bar plot
    data_execution = df[df['Measurement'] == 'Execution Time']
    ax = sns.barplot(x='case', y='time', hue='Configuration', data=data_execution,
                     palette=['tab:blue', 'tab:blue', 'tab:orange', 'tab:orange'], zorder=3, edgecolor='black')
    for j, bar in enumerate(ax.patches):
        if (j // len(data_execution['case'].unique())) % 2 == 1:
            bar.set_hatch('////')

    # adapt style
    plt.xlabel('Scale Factor')
    plt.ylabel('Query Execution Time [ms]')
    handles = ax.get_legend_handles_labels()[0]
    handles[1].set_hatch('////')
    handles[3].set_hatch('////')
    for handle in handles:
        handle.set_label(handle.get_label().replace('split', '\\textsc{split}'))
        handle.set_label(handle.get_label().replace('holistic', '\\textsc{holistic}'))
        handle.set_label(handle.get_label().replace('DPccp', '$\\textit{DP}_\\textit{ccp}$'))
        handle.set_label(handle.get_label().replace('PEall', '$\\textit{PE}_\\textit{all}$'))
    plt.legend(frameon=True, loc='upper left')
    if title:
        plt.title(title)
    plt.grid(axis='y', zorder=0)

    plt.savefig(f"fig/microbenchmark_{name.replace(' ', '_')}.pdf", transparent=True, bbox_inches='tight')

def create_tpch_job_table(data, queries):
    assert len(queries) % 2 == 0

    def create_tabular(file, query1, query2):
        def _round(value, digits):
            value = round(value, digits)
            return f'{{:.{digits}f}}'.format(value)

        def add_opt(data):
            data_split = data[data['Configuration'].str.contains('split')]
            data_topk = data[data['Configuration'].str.contains('topk')]
            data_holistic = data[data['Configuration'].str.contains('holistic')]

            split = data_split[data_split['Measurement'] == 'Optimization Time (algebraic)'].get('time').iloc[0] + data_split[data_split['Measurement'] == 'Optimization Time (physical)'].get('time').iloc[0]
            topk = data_topk[data_topk['Measurement'] == 'Optimization Time (algebraic)'].get('time').iloc[0] + data_topk[data_topk['Measurement'] == 'Optimization Time (physical)'].get('time').iloc[0]
            holistic = data_holistic[data_holistic['Measurement'] == 'Optimization Time'].get('time').iloc[0]

            file.write(f'& {_round(split, 1)} & {_round(topk, 1)} & \\hspace{{\\gap}}({_round(split/topk, 2)}) & {_round(holistic, 1)} & \\hspace{{\\gap}}({_round(split/holistic, 2)}) ')

        def add_run(data, idx):
            assert idx in [1, 2, 3, 4]

            data_split = data[data['Configuration'].str.contains('split')]
            data_topk = data[data['Configuration'].str.contains('topk')]
            data_holistic = data[data['Configuration'].str.contains('holistic')]

            split = data_split[data_split['Measurement'] == 'Execution Time'].get('time').iloc[0]
            topk = data_topk[data_topk['Measurement'] == 'Execution Time'].get('time').iloc[0]
            holistic = data_holistic[data_holistic['Measurement'] == 'Execution Time'].get('time').iloc[0]
            topk_holistic = mean([topk, holistic])

            match idx:
                case 1 | 3:
                    file.write(f'& {_round(split, 1)} & \\multicolumn{{4}}{{c!{{\\vrule width \\widthenum}}}}{{{_round(topk_holistic, 1)} ({_round(split/topk_holistic, 2)})}} ')
                case 2:
                    file.write(f'& {_round(split, 1)} & \\multicolumn{{4}}{{c!{{\\vrule width \\widthgrid}}}}{{{_round(topk_holistic, 1)} ({_round(split/topk_holistic, 2)})}} ')
                case 4:
                    file.write(f'& {_round(split, 1)} & \\multicolumn{{4}}{{c|}}{{{_round(topk_holistic, 1)} ({_round(split/topk_holistic, 2)})}} ')

        def add_sum(data):
            def color(value):
                if float(value) == 1.0:
                    return f'{value}'
                elif float(value) < 1.0:
                    return f'\\textcolor{{red!95!black}}{{{value}}}'
                else:
                    return f'\\textcolor{{green!60!black}}{{{value}}}'

            data_split = data[data['Configuration'].str.contains('split')]
            data_topk = data[data['Configuration'].str.contains('topk')]
            data_holistic = data[data['Configuration'].str.contains('holistic')]

            topk = data_topk[data_topk['Measurement'] == 'Execution Time'].get('time').iloc[0]
            holistic = data_holistic[data_holistic['Measurement'] == 'Execution Time'].get('time').iloc[0]
            topk_holistic = mean([topk, holistic])

            split = data_split[data_split['Measurement'] == 'Optimization Time (algebraic)'].get('time').iloc[0] + data_split[data_split['Measurement'] == 'Optimization Time (physical)'].get('time').iloc[0] + data_split[data_split['Measurement'] == 'Execution Time'].get('time').iloc[0]
            topk = data_topk[data_topk['Measurement'] == 'Optimization Time (algebraic)'].get('time').iloc[0] + data_topk[data_topk['Measurement'] == 'Optimization Time (physical)'].get('time').iloc[0] + topk_holistic
            holistic = data_holistic[data_holistic['Measurement'] == 'Optimization Time'].get('time').iloc[0] + topk_holistic

            file.write(f'& {_round(split, 1)} & {_round(topk, 1)} & \\hspace{{\\gap}}({color(_round(split/topk, 2))}) & {_round(holistic, 1)} & \\hspace{{\\gap}}({color(_round(split/holistic, 2))}) ')

        data1 = get_data(query1[0], query1[1], data)
        data2 = get_data(query2[0], query2[1], data)

        name1 = f'{BENCHMARK_NAMES[query1[0]]} {query1[1].split()[-1][1:-1]}'
        name2 = f'{BENCHMARK_NAMES[query2[0]]} {query2[1].split()[-1][1:-1]}'

        file.write(
            f'    \\begin{{tabular}}{{|l!{{\\vrule width \\widthgrid}}\n'
            f'                   R{{\\timesplitcellwidth}}|R{{\\timetopkcellwidth}}R{{\\factorcellwidth}}|R{{\\timeholisticcellwidth}}R{{\\factorcellwidth}}!{{\\vrule width \\widthenum}}R{{\\timesplitcellwidth}}|R{{\\timetopkcellwidth}}R{{\\factorcellwidth}}|R{{\\timeholisticcellwidth}}R{{\\factorcellwidth}}!{{\\vrule width \\widthgrid}}\n'
            f'                    R{{\\timesplitcellwidth}}|R{{\\timetopkcellwidth}}R{{\\factorcellwidth}}|R{{\\timeholisticcellwidth}}R{{\\factorcellwidth}}!{{\\vrule width \\widthenum}}R{{\\timesplitcellwidth}}|R{{\\timetopkcellwidth}}R{{\\factorcellwidth}}|R{{\\timeholisticcellwidth}}R{{\\factorcellwidth}}|}}\n'
            f'        \\hline\n'
            f'        \\textbf{{Query}} & \\multicolumn{{10}}{{c!{{\\vrule width \\widthgrid}}}}{{\\textbf{{{name1}}}}}\n'
            f'    	               & \\multicolumn{{10}}{{c|}}{{\\textbf{{{name2}}}}} \\\\\n'
            f'        \\hline\n'
            f'        \\textbf{{Join Enumeration}} & \\multicolumn{{5}}{{c!{{\\vrule width \\widthenum}}}}{{\\textbf{{\\DPccp}}}}\n'
            f'                                  & \\multicolumn{{5}}{{c!{{\\vrule width \\widthgrid}}}}{{\\textbf{{\\PEall}}}}\n'
            f'                                  & \\multicolumn{{5}}{{c!{{\\vrule width \\widthenum}}}}{{\\textbf{{\\DPccp}}}}\n'
            f'                                  & \\multicolumn{{5}}{{c|}}{{\\textbf{{\\PEall}}}} \\\\\n'
            f'        \\hline\n'
            f'        \\textbf{{Approach}} & \\multicolumn{{1}}{{c|}}{{\\textbf{{\\split}}}} & \\multicolumn{{2}}{{c|}}{{\\textbf{{\\topkconfig{{{query1[2]}}}}}}} & \\multicolumn{{2}}{{c!{{\\vrule width \\widthenum}}}}{{\\textbf{{\\holistic}}}}\n'
            f'                          & \\multicolumn{{1}}{{c|}}{{\\textbf{{\\split}}}} & \\multicolumn{{2}}{{c|}}{{\\textbf{{\\topkconfig{{{query1[3]}}}}}}} & \\multicolumn{{2}}{{c!{{\\vrule width \\widthgrid}}}}{{\\textbf{{\\holistic}}}}\n'
            f'                          & \\multicolumn{{1}}{{c|}}{{\\textbf{{\\split}}}} & \\multicolumn{{2}}{{c|}}{{\\textbf{{\\topkconfig{{{query2[2]}}}}}}} & \\multicolumn{{2}}{{c!{{\\vrule width \\widthenum}}}}{{\\textbf{{\\holistic}}}}\n'
            f'                          & \\multicolumn{{1}}{{c|}}{{\\textbf{{\\split}}}} & \\multicolumn{{2}}{{c|}}{{\\textbf{{\\topkconfig{{{query2[3]}}}}}}} & \\multicolumn{{2}}{{c|}}{{\\textbf{{\\holistic}}}} \\\\\n'
            f'        \\noalign{{\\hrule height \\widthgrid}}\n'
            f'        Opt. Time [ms] '
        )
        add_opt(data1[data1['Configuration'].str.contains('DPccp')])
        add_opt(data1[data1['Configuration'].str.contains('PEall')])
        add_opt(data2[data2['Configuration'].str.contains('DPccp')])
        add_opt(data2[data2['Configuration'].str.contains('PEall')])
        file.write(
            '\\\\\n'
            '        \\hline\n'
            '	     Running Time [ms] '
        )
        add_run(data1[data1['Configuration'].str.contains('DPccp')], 1)
        add_run(data1[data1['Configuration'].str.contains('PEall')], 2)
        add_run(data2[data2['Configuration'].str.contains('DPccp')], 3)
        add_run(data2[data2['Configuration'].str.contains('PEall')], 4)
        file.write(
            '\\\\\n'
            '        \\noalign{\\hrule height \\widthsum}\n'
            '        \\rowcolor{gray!20}\n'
            '        $\\sum$ [ms]'
        )
        add_sum(data1[data1['Configuration'].str.contains('DPccp')])
        add_sum(data1[data1['Configuration'].str.contains('PEall')])
        add_sum(data2[data2['Configuration'].str.contains('DPccp')])
        add_sum(data2[data2['Configuration'].str.contains('PEall')])
        file.write('\\\\\n'
            '        \\hline\n'
            '    \\end{tabular}\n'
        )

    with open('tbl-eval-real_world_benchmarks.tex', 'w') as f:
        f.write(
            '\\begin{table*}[t]\n'
            '    \\setlength\\tabcolsep{3pt}\n'
            '    \\def\\timesplitcellwidth{0.63cm}\n'
            '    \\def\\timetopkcellwidth{0.63cm}\n'
            '    \\def\\timeholisticcellwidth{0.75cm}\n'
            '    \\def\\factorcellwidth{0.39cm}\n'
            '    \\def\\widthgrid{1.2pt}\n'
            '    \\def\\widthenum{1pt}\n'
            '    \\def\\widthsum{0.8pt}\n'
            '    \\def\\gap{-0.16cm}\n'
            '    \\centering\n'
            '    \\footnotesize\n'
        )
        for i in range(0, len(queries), 2):
            create_tabular(f, queries[i], queries[i+1])
            if i != len(queries) - 2:
                f.write('    \\vspace{0.08cm}\\\\\n')
        f.write(
            '    \\caption{Performance of \\split compared to \\topk and \\holistic for varying join enumeration algoprithms on TPC-H and JOB.  For \\topk and \\holistic, we also specify the speedup over \\split in parantheses, \\ie $\\nicefrac{t_\\text{\\split}}{t_\\text{\\topk}}$ or $\\nicefrac{t_\\text{\\split}}{t_\\text{\\holistic}}$, respectively.  All measurements are rounded to one decimal place.}\n'
            '    \\label{tbl:real_world_benchmarks}\n'
            '\\end{table*}'
        )


if __name__ == '__main__':
    # load and preprocess data
    orig_data = pd.read_csv(DATA, usecols=COLUMN_TYPES.keys(), dtype=COLUMN_TYPES)
    orig_data['name'] = orig_data['name'].str[22:-1] # remove 'mutable (single core,'

    # plot solution space
    plot_solution_space()

    # plot optimization time
    plot_optimization(orig_data)
    plot_optimization_split(orig_data)

    # plot microbenchmarks
    plot_microbenchmark(orig_data, 'sortedness', None, [], 'Exploiting Sortedness')
    plot_microbenchmark(orig_data, 'fused', None, [], 'Exploiting Group-Join')
    plot_microbenchmark(orig_data, 'join order', None, [], 'Removing Algebraic Cost Abstraction')

    # create TPC-H and JOB table
    queries = [
        ('tpch', 'fused (q3)', 2, 2),
        ('job', 'sortedness (q5c)', 3, 3),
        ('job', 'join order (q8c)', 2, 2),
        ('job', 'join order (q11a)', 35, 2),
    ]
    create_tpch_job_table(orig_data, queries)
