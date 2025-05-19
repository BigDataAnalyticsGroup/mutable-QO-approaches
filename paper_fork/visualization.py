import seaborn as sns
import pandas as pd
import numpy as np
import matplotlib.pyplot as plt
import matplotlib.ticker as tkr
from matplotlib.patches import Polygon
import os
from statistics import mean

# set style
plt.style.use('/mutable/paper/.matplotlib/matplotlibrc')

DATA = os.path.join('../eval.out')
DATA_GUIDELINE = os.path.join('../guideline_decision.out')
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
    c_split = '#7851A9'
    c_holistic = '#82ad2a'
    c_top_k = '#00CED1'

    # create plot
    plt.figure(figsize=set_size(0.7, 0.14), layout='constrained')
    plt.xlim(0, 2)
    plt.ylim(0, 1.2)

    # add split, holistic, and pareto-optimum
    plt.gca().scatter(split[0], split[1], marker='x', color=c_split)
    plt.text(split[0] + .03, split[1] + .06, s='\\textsc{split}', ha='left', va='center', color=c_split)
    plt.gca().scatter(holistic[0], holistic[1], marker='x', color=c_holistic)
    plt.text(holistic[0] - .02, holistic[1] - .06, s='\\textsc{holistic}', ha='right', va='center', color=c_holistic)
    plt.gca().scatter(pareto[0], pareto[1], marker='x', color='black')
    plt.text(pareto[0] + .03, pareto[1] + .05, s='Pareto', ha='left', va='center')
    plt.text(pareto[0] + .03, pareto[1] - .05, s='Optimum', ha='left', va='center')

    # add top-k
    topk = Polygon([split, (0.5,1), (1.9,0.15), (0.3,0.15)], color=c_top_k, alpha=.5, edgecolor=None)
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

        # filter number of relations
        df = df[df['case'] % 2 == 0]

        df_holistic = df[(df['Configuration'] == 'holistic (DPccp)') | (df['Configuration'] == 'holistic (TDbasic)') | (df['Configuration'] == 'holistic (PEall)')]
        df_holistic_pruned = df[(df['Configuration'] == 'holistic with pruning (DPccp)') | (df['Configuration'] == 'holistic with pruning (TDbasic)') | (df['Configuration'] == 'holistic with pruning (PEall)')]
        df_split = df[(df['Configuration'] == 'split (DPccp)') | (df['Configuration'] == 'split (TDbasic)') | (df['Configuration'] == 'split (PEall)')]
        df_top5 = df[(df['Configuration'] == 'top-5 (DPccp)') | (df['Configuration'] == 'top-5 (TDbasic)') | (df['Configuration'] == 'top-5 (PEall)')]
        df_top10 = df[(df['Configuration'] == 'top-10 (DPccp)') | (df['Configuration'] == 'top-10 (TDbasic)') | (df['Configuration'] == 'top-10 (PEall)')]

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

        df = pd.concat([df_holistic, df_holistic_pruned, df_split, df_top5, df_top10])
        configuration_order = ['split (DPccp)', 'split (TDbasic)', 'split (PEall)',
                               'top-5 (DPccp)', 'top-5 (TDbasic)', 'top-5 (PEall)',
                               'top-10 (DPccp)', 'top-10 (TDbasic)', 'top-10 (PEall)',
                               'holistic with pruning (DPccp)', 'holistic with pruning (TDbasic)', 'holistic with pruning (PEall)',
                               'holistic (DPccp)', 'holistic (TDbasic)', 'holistic (PEall)']
        df['Configuration'] = pd.Categorical(df['Configuration'], categories=configuration_order, ordered=True)
        df = df.sort_values(by=['Configuration', 'case'])

        df.insert(0, "Shape", query_graph_shape.capitalize())
        return df

    df = pd.concat([preprocess_data('chain'), preprocess_data('cycle'), preprocess_data('star'), preprocess_data('clique')])

    # create a 2x2 grid of subplots
    fig, axes = plt.subplots(2, 2, figsize=set_size(2.0, 0.315), layout='constrained', sharex=True, sharey=True)

    # define the plot groups
    plot_groups = df['Shape'].unique()

    # loop through the plot groups and plot each group in a subplot
    for i, group in enumerate(plot_groups):
        # select the subplot
        ax = axes[i // 2, i % 2]

        # filter data for the current plot group
        data_group = df[df['Shape'] == group]

        # create a grouped bar plot
        palette=['tab:blue', 'tab:blue', 'tab:blue',
                 'tab:olive', 'tab:olive', 'tab:olive',
                 'tab:green', 'tab:green', 'tab:green',
                 'tab:red', 'tab:red', 'tab:red',
                 'tab:orange', 'tab:orange', 'tab:orange']
        unique_cat = data_group['case'].unique()
        unique_sub = data_group['Configuration'].unique()
        bar_width = 0.05
        subgroup_size = 3
        subgroup_spacing = 0.03  # extra spacing after every subgroup
        x = np.arange(len(unique_cat))
        for i, sub in enumerate(unique_sub):
            values = data_group[data_group['Configuration'] == sub]['time']
            num_subgroup = (i // subgroup_size) - (len(unique_sub) / subgroup_size) / 2
            offset = (i - len(unique_sub) / 2) * bar_width + num_subgroup * subgroup_spacing + bar_width / 2
            ax.bar(x + offset, values, width=bar_width, label=sub, color=palette[i], zorder=3, edgecolor='black', linewidth=0.5)
            ax.set_xticks(x, unique_cat)
        # sns.barplot(x='case', y='time', hue='Configuration', data=data_group, ax=ax, palette=palette, zorder=3, edgecolor='black') # edgecolor=None

        for j, bar in enumerate(ax.patches):
            if (j // len(data_group['case'].unique())) % 3 == 1:
                bar.set_hatch('\\\\\\\\')
            if (j // len(data_group['case'].unique())) % 3 == 2:
                bar.set_hatch('////')
        ax.set_yscale('log')

        ## set y ticks
        y_minor = tkr.LogLocator(base=10.0, subs=np.arange(1.0, 10.0) * 0.1, numticks=10)
        ax.yaxis.set_minor_locator(y_minor)
        ax.yaxis.set_minor_formatter(tkr.NullFormatter())

        # adapt style
        ax.set_title(group)
        handles, labels = ax.get_legend_handles_labels()
        ax.legend().remove()
        ax.grid(None)
        ax.grid(axis='y', zorder=0)
        ax.margins(x=0.01)

    # adapt common style
    fig.supxlabel('\#Relations', x=0.53, y=-0.02)
    fig.supylabel('Query Optimization Time [ms] (log-scale)', y=0.54)
    for i in range(len(labels)):
        labels[i] = labels[i].replace('split', '\\textsc{split}')
        labels[i] = labels[i].replace('top-5', '\\textsc{top-5}')
        labels[i] = labels[i].replace('top-10', '\\textsc{top-10}')
        labels[i] = labels[i].replace('holistic with pruning', '$\\textsc{holistic}_\\textsc{opt}$')
        labels[i] = labels[i].replace('holistic', '\\textsc{holistic}')
        labels[i] = labels[i].replace('DPccp', '$\\textit{DP}_\\textit{ccp}$')
        labels[i] = labels[i].replace('TDbasic', '$\\textit{TD}_\\textit{basic}$')
        labels[i] = labels[i].replace('PEall', '$\\textit{PE}_\\textit{all}$')
    fig.legend(handles, labels, frameon=False, ncols=5, bbox_to_anchor=(0.96, 1.19))

    plt.savefig('fig/optimization.pdf', transparent=True, bbox_inches='tight')

def plot_microbenchmark(data, name, minimal_sf=None, except_sf=[], title=None):
    def merge_plan_enumerators(data):
        data = data.reset_index()
        config_name = data.iloc[0]['Configuration'].split(' (')[0]
        data['Configuration'] = data['Configuration'].str[:len(config_name)] # remove plan enumerator name
        data = data.groupby(['Configuration', 'Measurement', 'case'])['time'].mean().reset_index()
        return data

    df = get_data('microbenchmark', name, data)
    df['case'] = pd.to_numeric(df['case'])

    if minimal_sf:
        df = df[df['case'] >= minimal_sf]
    for sf in except_sf:
        df = df[df['case'] != sf]

    df_split = merge_plan_enumerators(df[df['Configuration'].str.contains('split')])
    df_topk = merge_plan_enumerators(df[df['Configuration'].str.contains('top-2')])
    df_holistic_pruned = merge_plan_enumerators(df[df['Configuration'].str.contains('holistic with pruning')])
    df_holistic = merge_plan_enumerators(df[df['Configuration'].str.contains('holistic') & ~(df['Configuration'].str.contains('pruning'))])

    # add algebraic and physical optimization time for split and top-k approach
    pivot_df = df_split.pivot(index=['Configuration', 'case'], columns='Measurement', values='time')
    pivot_df['Optimization Time'] = pivot_df['Optimization Time (algebraic)'] + pivot_df['Optimization Time (physical)']
    df_split = pivot_df.reset_index().melt(id_vars=['Configuration', 'case'], var_name='Measurement',
                                           value_name='time').dropna()
    pivot_df = df_topk.pivot(index=['Configuration', 'case'], columns='Measurement', values='time')
    pivot_df['Optimization Time'] = pivot_df['Optimization Time (algebraic)'] + pivot_df['Optimization Time (physical)']
    df_topk = pivot_df.reset_index().melt(id_vars=['Configuration', 'case'], var_name='Measurement',
                                          value_name='time').dropna()
    df_split = df_split[
        (df_split['Measurement'] == 'Optimization Time') | (df_split['Measurement'] == 'Execution Time')]
    df_topk = df_topk[
        (df_topk['Measurement'] == 'Optimization Time') | (df_topk['Measurement'] == 'Execution Time')]

    df = pd.concat([df_holistic, df_holistic_pruned, df_topk, df_split])

    # add optimization to running time as measurement of end-to-end performance
    pivot_df = df.pivot(index=['Configuration', 'case'], columns='Measurement', values='time')
    pivot_df['Execution Time'] = pivot_df['Optimization Time'] + pivot_df['Execution Time']
    df = pivot_df.reset_index().melt(id_vars=['Configuration', 'case'], var_name='Measurement',
                                     value_name='time').dropna()

    configuration_order = ['split', 'top-2', 'holistic with pruning', 'holistic']
    df['Configuration'] = pd.Categorical(df['Configuration'], categories=configuration_order, ordered=True)
    df = df.sort_values(by='Configuration')

    # create a plot
    plt.figure(figsize=set_size(1.0, 0.175), layout='constrained')

    # define the configurations
    configurations = df['Configuration'].unique()

    # create a stacked grouped bar plot
    data_execution = df[df['Measurement'] == 'Execution Time']
    palette = ['tab:blue', 'tab:olive', 'tab:red', 'tab:orange']
    ax = sns.barplot(x='case', y='time', hue='Configuration', data=data_execution, palette=palette, zorder=3,
                     edgecolor='black')

    # adapt style
    plt.xlabel('Scale Factor (log-scale)')
    plt.ylabel('Query Execution Time [ms]')
    handles = ax.get_legend_handles_labels()[0]
    for handle in handles:
        handle.set_label(handle.get_label().replace('split', '\\textsc{split}'))
        handle.set_label(handle.get_label().replace('top-2', '\\textsc{top-2}'))
        handle.set_label(handle.get_label().replace('holistic with pruning', '$\\textsc{holistic}_\\textsc{opt}$'))
        handle.set_label(handle.get_label().replace('holistic', '\\textsc{holistic}'))
    plt.legend(frameon=True, loc='upper left')
    if title:
        plt.title(title)
    plt.grid(axis='y', zorder=0)

    plt.savefig(f"fig/microbenchmark_{name.replace(' ', '_')}.pdf", transparent=True, bbox_inches='tight')

def create_tpch_job_table(_data, queries):
    def create_tabular(file, query):
        def _round(value, digits):
            value = round(value, digits)
            return f'{{:.{digits}f}}'.format(value)

        def add_opt(data):
            data_split = data[data['Configuration'].str.contains('split')]
            data_topk = data[data['Configuration'].str.contains('topk')]
            data_holistic_pruned = data[data['Configuration'].str.contains('holistic with pruning')]
            data_holistic = data[data['Configuration'].str.contains('holistic') & ~(data['Configuration'].str.contains('pruning'))]

            split = data_split[data_split['Measurement'] == 'Optimization Time (algebraic)'].get('time').iloc[0] + data_split[data_split['Measurement'] == 'Optimization Time (physical)'].get('time').iloc[0]
            topk = data_topk[data_topk['Measurement'] == 'Optimization Time (algebraic)'].get('time').iloc[0] + data_topk[data_topk['Measurement'] == 'Optimization Time (physical)'].get('time').iloc[0]
            holistic_pruned = data_holistic_pruned[data_holistic_pruned['Measurement'] == 'Optimization Time'].get('time').iloc[0]
            holistic = data_holistic[data_holistic['Measurement'] == 'Optimization Time'].get('time').iloc[0]

            file.write(f'& {_round(split, 1)} & {_round(topk, 1)} & \\hspace{{\\gap}}({_round(split/topk, 2)}) & {_round(holistic_pruned, 1)} & \\hspace{{\\gap}}({_round(split/holistic_pruned, 2)}) & {_round(holistic, 1)} & \\hspace{{\\gap}}({_round(split/holistic, 2)}) ')

        def add_run(data, idx):
            assert idx in [1, 2, 3]

            data_split = data[data['Configuration'].str.contains('split')]
            data_topk = data[data['Configuration'].str.contains('topk')]
            data_holistic_pruned = data[data['Configuration'].str.contains('holistic with pruning')]
            data_holistic = data[data['Configuration'].str.contains('holistic') & ~(data['Configuration'].str.contains('pruning'))]

            split = data_split[data_split['Measurement'] == 'Execution Time'].get('time').iloc[0]
            topk = data_topk[data_topk['Measurement'] == 'Execution Time'].get('time').iloc[0]
            holistic_pruned = data_holistic_pruned[data_holistic_pruned['Measurement'] == 'Execution Time'].get('time').iloc[0]
            holistic = data_holistic[data_holistic['Measurement'] == 'Execution Time'].get('time').iloc[0]
            topk_holistic = mean([topk, holistic_pruned, holistic])

            match idx:
                case 1 | 2:
                    file.write(f'& {_round(split, 1)} & \\multicolumn{{6}}{{c!{{\\vrule width \\widthenum}}}}{{{_round(topk_holistic, 1)} ({_round(split/topk_holistic, 2)})}} ')
                case 3:
                    file.write(f'& {_round(split, 1)} & \\multicolumn{{6}}{{c|}}{{{_round(topk_holistic, 1)} ({_round(split/topk_holistic, 2)})}} ')

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
            data_holistic_pruned = data[data['Configuration'].str.contains('holistic with pruning')]
            data_holistic = data[data['Configuration'].str.contains('holistic') & ~(data['Configuration'].str.contains('pruning'))]

            topk = data_topk[data_topk['Measurement'] == 'Execution Time'].get('time').iloc[0]
            holistic_pruned = data_holistic_pruned[data_holistic_pruned['Measurement'] == 'Execution Time'].get('time').iloc[0]
            holistic = data_holistic[data_holistic['Measurement'] == 'Execution Time'].get('time').iloc[0]
            topk_holistic = mean([topk, holistic_pruned, holistic])

            split = data_split[data_split['Measurement'] == 'Optimization Time (algebraic)'].get('time').iloc[0] + data_split[data_split['Measurement'] == 'Optimization Time (physical)'].get('time').iloc[0] + data_split[data_split['Measurement'] == 'Execution Time'].get('time').iloc[0]
            topk = data_topk[data_topk['Measurement'] == 'Optimization Time (algebraic)'].get('time').iloc[0] + data_topk[data_topk['Measurement'] == 'Optimization Time (physical)'].get('time').iloc[0] + topk_holistic
            holistic_pruned = data_holistic_pruned[data_holistic_pruned['Measurement'] == 'Optimization Time'].get('time').iloc[0] + topk_holistic
            holistic = data_holistic[data_holistic['Measurement'] == 'Optimization Time'].get('time').iloc[0] + topk_holistic

            file.write(f'& {_round(split, 1)} & {_round(topk, 1)} & \\hspace{{\\gap}}({color(_round(split/topk, 2))}) & {_round(holistic_pruned, 1)} & \\hspace{{\\gap}}({color(_round(split/holistic_pruned, 2))}) & {_round(holistic, 1)} & \\hspace{{\\gap}}({color(_round(split/holistic, 2))}) ')

        data = get_data(query[0], query[1], _data)

        name = f'{BENCHMARK_NAMES[query[0]]} {query[1].split()[-1][1:-1]}'

        file.write(
            f'    \\begin{{tabular}}{{|l!{{\\vrule width \\widthgrid}}\n'
            f'                        R{{\\timesplitcellwidth}}|R{{\\timetopkcellwidth}}R{{\\factorcellwidth}}|R{{\\timeholisticprunedcellwidth}}R{{\\factorcellwidth}}|R{{\\timeholisticcellwidth}}R{{\\factorcellwidth}}'
            f'                        !{{\\vrule width \\widthenum}}'
            f'                        R{{\\timesplitcellwidth}}|R{{\\timetopkcellwidth}}R{{\\factorcellwidth}}|R{{\\timeholisticprunedcellwidth}}R{{\\factorcellwidth}}|R{{\\timeholisticcellwidth}}R{{\\factorcellwidth}}'
            f'                        !{{\\vrule width \\widthenum}}'
            f'                        R{{\\timesplitcellwidth}}|R{{\\timetopkcellwidth}}R{{\\factorcellwidth}}|R{{\\timeholisticprunedcellwidth}}R{{\\factorcellwidth}}|R{{\\timeholisticcellwidth}}R{{\\factorcellwidth}}'
            f'                        |}}\n'
            f'        \\hline\n'
            f'        \\textbf{{Query}} & \\multicolumn{{21}}{{c|}}{{\\textbf{{{name}}}}} \\\\\n'
            f'        \\hline\n'
            f'        \\textbf{{Join Enumeration}} & \\multicolumn{{7}}{{c!{{\\vrule width \\widthenum}}}}{{\\textbf{{\\DPccp}}}}\n'
            f'                                     & \\multicolumn{{7}}{{c!{{\\vrule width \\widthenum}}}}{{\\textbf{{\\TDbasic}}}}\n'
            f'                                     & \\multicolumn{{7}}{{c|}}{{\\textbf{{\\PEall}}}} \\\\\n'
            f'        \\hline\n'
            f'        \\textbf{{Approach}} & \\multicolumn{{1}}{{c|}}{{\\textbf{{\\split}}}} & \\multicolumn{{2}}{{c|}}{{\\textbf{{\\topkconfig{{{query[2]}}}}}}} & \\multicolumn{{2}}{{c|}}{{\\textbf{{\\holisticpruned}}}} & \\multicolumn{{2}}{{c!{{\\vrule width \\widthenum}}}}{{\\textbf{{\\holistic}}}}\n'
            f'                             & \\multicolumn{{1}}{{c|}}{{\\textbf{{\\split}}}} & \\multicolumn{{2}}{{c|}}{{\\textbf{{\\topkconfig{{{query[3]}}}}}}} & \\multicolumn{{2}}{{c|}}{{\\textbf{{\\holisticpruned}}}} & \\multicolumn{{2}}{{c!{{\\vrule width \\widthenum}}}}{{\\textbf{{\\holistic}}}}\n'
            f'                             & \\multicolumn{{1}}{{c|}}{{\\textbf{{\\split}}}} & \\multicolumn{{2}}{{c|}}{{\\textbf{{\\topkconfig{{{query[4]}}}}}}} & \\multicolumn{{2}}{{c|}}{{\\textbf{{\\holisticpruned}}}} & \\multicolumn{{2}}{{c|}}{{\\textbf{{\\holistic}}}} \\\\\n'
            f'        \\noalign{{\\hrule height \\widthgrid}}\n'
            f'        Optimization Time [ms] '
        )
        add_opt(data[data['Configuration'].str.contains('DPccp')])
        add_opt(data[data['Configuration'].str.contains('TDbasic')])
        add_opt(data[data['Configuration'].str.contains('PEall')])
        file.write(
            '\\\\\n'
            '        \\hline\n'
            '	     Running Time [ms] '
        )
        add_run(data[data['Configuration'].str.contains('DPccp')], 1)
        add_run(data[data['Configuration'].str.contains('TDbasic')], 2)
        add_run(data[data['Configuration'].str.contains('PEall')], 3)
        file.write(
            '\\\\\n'
            '        \\noalign{\\hrule height \\widthsum}\n'
            '        \\rowcolor{gray!20}\n'
            '        $\\sum$ [ms]'
        )
        add_sum(data[data['Configuration'].str.contains('DPccp')])
        add_sum(data[data['Configuration'].str.contains('TDbasic')])
        add_sum(data[data['Configuration'].str.contains('PEall')])
        file.write('\\\\\n'
            '        \\hline\n'
            '    \\end{tabular}\n'
        )

    with open('tbl-eval-real_world_benchmarks.tex', 'w') as f:
        f.write(
            '\\begin{table*}[t]\n'
            '    \\setlength\\tabcolsep{3pt}\n'
            '    \\def\\timesplitcellwidth{0.54cm}\n'
            '    \\def\\timetopkcellwidth{0.54cm}\n'
            '    \\def\\timeholisticprunedcellwidth{0.64cm}\n'
            '    \\def\\timeholisticcellwidth{0.74cm}\n'
            '    \\def\\factorcellwidth{0.39cm}\n'
            '    \\def\\widthgrid{1.2pt}\n'
            '    \\def\\widthenum{1pt}\n'
            '    \\def\\widthsum{0.8pt}\n'
            '    \\def\\gap{-0.16cm}\n'
            '    \\centering\n'
            '    \\scriptsize\n'
        )
        for i in range(0, len(queries)):
            create_tabular(f, queries[i])
            if i != len(queries) - 1:
                f.write('    \\vspace{0.08cm}\\\\\n')
        f.write(
            '    \\captionsetup{labelfont={color=c_revisionadd,bf}}'
            '    \\caption{Performance of \\split compared to \\topk, \\revisionadd{\\holisticpruned}, and \\holistic for varying join enumeration \\revisionrewrite{algorithms} on TPC-H and JOB.  For \\topk, \\revisionadd{\\holisticpruned}, and \\holistic, we also specify the speedup over \\split in parantheses, \\ie $\\nicefrac{t_\\text{\\split}}{t_\\text{\\topk}}$, \\revisionadd{$\\nicefrac{t_\\text{\\split}}{t_\\text{\\holisticpruned}}$}, or $\\nicefrac{t_\\text{\\split}}{t_\\text{\\holistic}}$, respectively.  All measurements are rounded to one decimal place.}\n'
            '    \\label{tbl:real_world_benchmarks}\n'
            '\\end{table*}'
        )

def plot_guideline(data, decision_data):
    data = data[(data['benchmark'] == 'tpch') | (data['benchmark'] == 'job') | (data['benchmark'] == 'ceb')]

    df = data.groupby(['benchmark', 'experiment', 'name', 'config', 'case'])['time'].median().reset_index()
    df.rename(columns={'name': 'Configuration', 'config': 'Measurement'}, inplace=True)

    df_split = df[df['Configuration'].str.contains('split')]
    df_topk = df[df['Configuration'].str.contains('top-')]
    df_holistic_pruned = df[df['Configuration'].str.contains('holistic with pruning')]
    df_holistic = df[df['Configuration'].str.contains('holistic') & ~(df['Configuration'].str.contains('pruning'))]

    # add algebraic and physical optimization time for split and top-k approach
    pivot_df = df_split.pivot(index=['benchmark', 'experiment', 'Configuration', 'case'], columns='Measurement', values='time')
    pivot_df['Optimization Time'] = pivot_df['Optimization Time (algebraic)'] + pivot_df['Optimization Time (physical)']
    df_split = pivot_df.reset_index().melt(id_vars=['benchmark', 'experiment', 'Configuration', 'case'], var_name='Measurement',
                                           value_name='time').dropna()
    pivot_df = df_topk.pivot(index=['benchmark', 'experiment', 'Configuration', 'case'], columns='Measurement', values='time')
    pivot_df['Optimization Time'] = pivot_df['Optimization Time (algebraic)'] + pivot_df['Optimization Time (physical)']
    df_topk = pivot_df.reset_index().melt(id_vars=['benchmark', 'experiment', 'Configuration', 'case'], var_name='Measurement',
                                          value_name='time').dropna()
    df_split = df_split[
        (df_split['Measurement'] == 'Optimization Time') | (df_split['Measurement'] == 'Execution Time')]
    df_topk = df_topk[
        (df_topk['Measurement'] == 'Optimization Time') | (df_topk['Measurement'] == 'Execution Time')]

    df = pd.concat([df_holistic, df_holistic_pruned, df_topk, df_split])

    # add optimization to running time as measurement of end-to-end performance
    pivot_df = df.pivot(index=['benchmark', 'experiment', 'Configuration', 'case'], columns='Measurement', values='time')
    pivot_df['Execution Time'] = pivot_df['Optimization Time'] + pivot_df['Execution Time']
    df = pivot_df.reset_index().melt(id_vars=['benchmark', 'experiment', 'Configuration', 'case'], var_name='Measurement',
                                     value_name='time').dropna()
    df = df[df['Measurement'] == 'Execution Time']

    # compute best and worst possible time
    df_factor = df.groupby(['benchmark', 'experiment'])['time'].agg(Best='min', Worst='max').reset_index()
    # print(df.loc[df.groupby(['benchmark', 'experiment'])['time'].idxmin()])

    # compute chosen time
    df_factor['Decision'] = df_factor.apply(
        lambda x: (decision := decision_data[(decision_data['benchmark'] == x['benchmark']) & (decision_data['experiment'] == x['experiment'])]['decision'],
                   decision.iloc[0] if decision.size == 1 else np.nan)[-1],
        axis=1
    )
    df_factor['Chosen'] = df_factor.apply(
        lambda x: (chosen := df[(df['benchmark'] == x['benchmark']) & (df['experiment'] == x['experiment']) & (df['Configuration'] == x['Decision'])]['time'],
                   chosen.iloc[0] if chosen.size == 1 else np.nan)[-1],
        axis=1
    )

    # add split and holistic with pruning measurements
    df_factor['Split'] = df_factor.apply(
        lambda x: (split := df[(df['benchmark'] == x['benchmark']) & (df['experiment'] == x['experiment']) & (df['Configuration'] == 'split (DPccp)')]['time'],
                   split.iloc[0] if split.size == 1 else np.nan)[-1],
        axis=1
    )
    df_factor['Top5'] = df_factor.apply(
        lambda x: (split := df[(df['benchmark'] == x['benchmark']) & (df['experiment'] == x['experiment']) & (df['Configuration'] == 'top-5 (DPccp)')]['time'],
                   split.iloc[0] if split.size == 1 else np.nan)[-1],
        axis=1
    )
    df_factor['Top10'] = df_factor.apply(
        lambda x: (split := df[(df['benchmark'] == x['benchmark']) & (df['experiment'] == x['experiment']) & (df['Configuration'] == 'top-10 (DPccp)')]['time'],
                   split.iloc[0] if split.size == 1 else np.nan)[-1],
        axis=1
    )
    df_factor['Holistic_Opt'] = df_factor.apply(
        lambda x: (holistic_opt := df[(df['benchmark'] == x['benchmark']) & (df['experiment'] == x['experiment']) & (df['Configuration'] == 'holistic with pruning (DPccp)')]['time'],
                   holistic_opt.iloc[0] if holistic_opt.size == 1 else np.nan)[-1],
        axis=1
    )
    df_factor['Holistic'] = df_factor.apply(
        lambda x: (holistic_opt := df[(df['benchmark'] == x['benchmark']) & (df['experiment'] == x['experiment']) & (df['Configuration'] == 'holistic (DPccp)')]['time'],
                   holistic_opt.iloc[0] if holistic_opt.size == 1 else np.nan)[-1],
        axis=1
    )

    # compute factors
    df_factor['Factor_Chosen']  = df_factor.apply(lambda x: x['Chosen'] / x['Best'], axis=1)
    df_factor['Factor_Worst'] = df_factor.apply(lambda x: x['Worst'] / x['Chosen'], axis=1)
    df_factor['Factor_Split'] = df_factor.apply(lambda x: x['Split'] / x['Best'], axis=1)
    df_factor['Factor_Top5'] = df_factor.apply(lambda x: x['Top5'] / x['Best'], axis=1)
    df_factor['Factor_Top10'] = df_factor.apply(lambda x: x['Top10'] / x['Best'], axis=1)
    df_factor['Factor_Holistic_Opt'] = df_factor.apply(lambda x: x['Holistic_Opt'] / x['Best'], axis=1)

    # print(df_factor[['experiment', 'Decision', 'Factor_Chosen', 'Factor_Split', 'Factor_Top5', 'Factor_Top10', 'Factor_Holistic_Opt']])

    # create a plot
    fig, axes = plt.subplots(1, 6, figsize=set_size(2.4, 0.12), gridspec_kw={'width_ratios': [1, 1, 1, 1, 0.05, 1]}, sharey=True)
    sns.violinplot(y=df_factor['Factor_Split'], color='tab:blue', zorder=0, edgecolor='black', ax=axes[0])
    sns.violinplot(y=df_factor['Factor_Top5'], color='tab:olive', zorder=0, edgecolor='black', ax=axes[1])
    sns.violinplot(y=df_factor['Factor_Top10'], color='tab:green', zorder=0, edgecolor='black', ax=axes[2])
    sns.violinplot(y=df_factor['Factor_Holistic_Opt'], color='tab:red', zorder=0, edgecolor='black', ax=axes[3])
    axes[4].set_visible(False) # skip dummy ax[4] for space
    sns.violinplot(y=df_factor['Factor_Chosen'], color='lightblue', zorder=0, edgecolor='black', ax=axes[5])

    # adapt style
    # fig.suptitle('Distribution of Values')
    axes[0].set_ylabel('Slowdown Factor\\newline\\hspace*{1cm}(log-scale)')
    axes[0].set_title('\\textsc{split}')
    axes[1].set_title('\\textsc{top-5}')
    axes[2].set_title('\\textsc{top-10}')
    axes[3].set_title('$\\textsc{holistic}_\\textsc{opt}$')
    axes[5].set_title('Guideline')
    for ax in axes.flatten():
        ax.tick_params(bottom=False)
        ax.set_yscale('log')
        ax.set_ylim(1, 140)
    plt.grid(True, zorder=0)

    plt.savefig(f"fig/guideline.pdf", transparent=True, bbox_inches='tight')


if __name__ == '__main__':
    # load and preprocess data
    orig_data = pd.read_csv(DATA, usecols=COLUMN_TYPES.keys(), dtype=COLUMN_TYPES)
    orig_data['name'] = orig_data['name'].str[22:-1] # remove 'mutable (single core,'
    orig_data_guideline = pd.read_csv(DATA_GUIDELINE, usecols=['benchmark', 'experiment', 'decision'])

    # plot solution space
    plot_solution_space()

    # plot optimization time
    plot_optimization(orig_data)

    # plot microbenchmarks
    plot_microbenchmark(orig_data, 'sortedness', None, [], 'Exploiting Sortedness')
    plot_microbenchmark(orig_data, 'fused', None, [], 'Exploiting Group-Join')
    plot_microbenchmark(orig_data, 'join order', None, [], 'Removing Algebraic Cost Abstraction')

    # create TPC-H and JOB table
    queries = [
        ('tpch', 'fused (q3)', 2, 2, 2),
        ('job', 'sortedness (q5c)', 3, 3, 3),
        ('job', 'join order (q8c)', 2, 2, 2),
        ('job', 'join order (q11a)', 35, 35, 2),
    ]
    create_tpch_job_table(orig_data, queries)

    # plot guideline
    plot_guideline(orig_data, orig_data_guideline)
